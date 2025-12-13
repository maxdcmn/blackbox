#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#ifdef NVML_AVAILABLE
#include <nvml.h>
#endif
#include <sstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct MemoryBlock {
    unsigned long long address;
    unsigned long long size;
    std::string type;  // "kv_cache", "activation", "weight", "other"
    int block_id;
    bool allocated;  // Whether block is allocated/reserved
    bool utilized;   // Whether block is actively being used (has data)
};

struct ProcessMemory {
    unsigned int pid;
    std::string name;
    unsigned long long used_bytes;
    unsigned long long reserved_bytes;
};

struct ThreadInfo {
    int thread_id;
    unsigned long long allocated_bytes;
    std::string state;
};

struct NsightMetrics {
    unsigned long long atomic_operations;
    unsigned long long threads_per_block;
    double occupancy;
    // Memory block activity metrics
    unsigned long long active_blocks;  // Blocks actively processing
    unsigned long long memory_throughput;  // Memory throughput (bytes/sec)
    unsigned long long dram_read_bytes;  // DRAM read bytes
    unsigned long long dram_write_bytes;  // DRAM write bytes
    bool available;
};

struct DetailedVRAMInfo {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
    unsigned long long reserved;
    // Note: blocks are vLLM KV cache blocks (application-level), not GPU CUDA blocks
    std::vector<MemoryBlock> blocks;
    // Process-level VRAM usage from NVML (system-level monitoring)
    std::vector<ProcessMemory> processes;
    // Note: threads are application-level request threads from vLLM, not GPU CUDA threads
    // For GPU thread/block/atomic metrics, use NVIDIA Nsight Compute (NCU) profiler
    std::vector<ThreadInfo> threads;
    unsigned int active_blocks;  // vLLM KV cache blocks
    unsigned int free_blocks;    // vLLM KV cache blocks
    // Atomic allocations: sum of all process memory from NVML
    unsigned long long atomic_allocations;
    double fragmentation_ratio;
    // Nsight Compute metrics per process (keyed by PID)
    std::map<unsigned int, NsightMetrics> nsight_metrics;
};

static nvmlDevice_t g_device = nullptr;
static bool g_nvml_initialized = false;

bool initNVML() {
    if (g_nvml_initialized) return true;
#ifdef NVML_AVAILABLE
    if (nvmlInit() == NVML_SUCCESS) {
        unsigned int deviceCount;
        if (nvmlDeviceGetCount(&deviceCount) == NVML_SUCCESS && deviceCount > 0) {
            if (nvmlDeviceGetHandleByIndex(0, &g_device) == NVML_SUCCESS) {
                g_nvml_initialized = true;
                return true;
            }
        }
    }
#endif
    return false;
}

void shutdownNVML() {
    if (g_nvml_initialized) {
#ifdef NVML_AVAILABLE
        nvmlShutdown();
#endif
        g_nvml_initialized = false;
        g_device = nullptr;
    }
}

void parseVLLMMetrics(const std::string& metrics, DetailedVRAMInfo& info);
NsightMetrics getNsightMetrics(unsigned int pid);

// Get detailed VRAM usage combining NVML (system-level) and vLLM metrics (application-level)
// 
// NVML Limitations (what we CANNOT get):
// - Per-thread GPU metrics (CUDA threads)
// - Per-block GPU metrics (CUDA blocks)  
// - Atomic operation counts
// - Shared memory bank conflicts
// - Instruction mix
// 
// For detailed GPU performance metrics, use NVIDIA Nsight Compute (NCU) profiler
// 
// What we CAN get from NVML:
// - Total/used/free VRAM (system-level)
// - Process-level VRAM usage by PID
// 
// What we get from vLLM metrics:
// - KV cache block counts (application-level memory blocks)
// - Request counts (application-level threads)
DetailedVRAMInfo getDetailedVRAMUsage(const std::string& vllm_metrics = "") {
    DetailedVRAMInfo detailed = {0, 0, 0, 0, {}, {}, {}, 0, 0, 0, 0.0};
    if (!initNVML()) return detailed;
#ifdef NVML_AVAILABLE
    nvmlMemory_t memory;
    if (nvmlDeviceGetMemoryInfo(g_device, &memory) == NVML_SUCCESS) {
        detailed.total = memory.total;
        detailed.used = memory.used;
        detailed.free = memory.free;
        detailed.reserved = memory.used;
    }

    unsigned int processCount = 64;
    nvmlProcessInfo_t processes[64];
    unsigned long long total_atomic_allocations = 0;
    
    if (nvmlDeviceGetComputeRunningProcesses(g_device, &processCount, processes) == NVML_SUCCESS) {
        for (unsigned int i = 0; i < processCount; ++i) {
            ProcessMemory pm;
            pm.pid = processes[i].pid;
            pm.used_bytes = processes[i].usedGpuMemory;
            pm.reserved_bytes = processes[i].usedGpuMemory;
            
            // Sum up all process memory allocations (atomic allocations per process)
            total_atomic_allocations += processes[i].usedGpuMemory;
            
            char name[256] = {0};
            FILE* fp = fopen(absl::StrCat("/proc/", pm.pid, "/comm").c_str(), "r");
            if (fp) {
                fgets(name, sizeof(name), fp);
                fclose(fp);
                pm.name = name;
                if (!pm.name.empty() && pm.name.back() == '\n') {
                    pm.name.pop_back();
                }
            } else {
                pm.name = "unknown";
            }
            detailed.processes.push_back(pm);
            
            // Get Nsight Compute metrics for this PID
            // This provides actual GPU block activity data
            NsightMetrics nsight = getNsightMetrics(pm.pid);
            if (nsight.available) {
                detailed.nsight_metrics[pm.pid] = nsight;
                std::cout << "[DEBUG] Nsight metrics for PID " << pm.pid 
                          << ": active_blocks=" << nsight.active_blocks
                          << ", dram_read=" << nsight.dram_read_bytes
                          << ", dram_write=" << nsight.dram_write_bytes << std::endl;
            }
        }
    }

    // Set atomic allocations to sum of all process memory allocations from NVML
    // This represents the total memory allocated atomically by all processes
    detailed.atomic_allocations = total_atomic_allocations > 0 ? total_atomic_allocations : detailed.used;

    // Default values (will be overridden by vLLM if available)
    detailed.active_blocks = detailed.processes.size();
    detailed.free_blocks = 0;
    detailed.fragmentation_ratio = detailed.total > 0 ? 
        (1.0 - (double)detailed.free / detailed.total) : 0.0;

    // Create thread info from processes (application-level, not GPU CUDA threads)
    // NVML only provides process-level VRAM usage, not per-thread GPU metrics
    for (size_t i = 0; i < detailed.processes.size(); ++i) {
        ThreadInfo ti;
        ti.thread_id = i;
        ti.allocated_bytes = detailed.processes[i].used_bytes;
        ti.state = "active";
        detailed.threads.push_back(ti);
    }
    
    // Parse vLLM metrics to populate blocks and update counts
    // This will use Nsight Compute data if available for more accurate block utilization
    if (!vllm_metrics.empty()) {
        parseVLLMMetrics(vllm_metrics, detailed);
    }
#endif
    return detailed;
}

std::string fetchVLLMEndpoint(const std::string& host, const std::string& port, const std::string& path) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(host, port);
        tcp::socket socket(ioc);
        net::connect(socket, results);
        
        http::request<http::string_body> req{http::verb::get, path, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "blackbox-server");
        
        http::write(socket, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(socket, buffer, res);
        
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_both, ec);
        
        if (res.result() == http::status::ok) {
            return res.body();
        }
    } catch (...) {
    }
    return "";
}

std::string fetchVLLMMetrics(const std::string& vllm_url = "http://localhost:8000") {
    return fetchVLLMEndpoint("localhost", "8000", "/metrics");
}

void parseVLLMMetrics(const std::string& metrics, DetailedVRAMInfo& info) {
    std::cout << "[DEBUG] parseVLLMMetrics called, metrics length=" << metrics.length() << std::endl;
    
    if (metrics.empty()) {
        std::cout << "[DEBUG] metrics is empty, returning" << std::endl;
        return;
    }
    
    // Handle JSON-escaped newlines
    std::string unescaped_metrics = metrics;
    size_t pos = 0;
    while ((pos = unescaped_metrics.find("\\n", pos)) != std::string::npos) {
        unescaped_metrics.replace(pos, 2, "\n");
        pos += 1;
    }
    
    std::istringstream iss(unescaped_metrics);
    std::string line;
    int num_gpu_blocks = 0;
    int block_size = 16;
    double kv_cache_usage = 0.0;
    int num_requests_running = 0;
    int num_requests_waiting = 0;
    
    while (std::getline(iss, line)) {
        if (line.find("cache_config_info") != std::string::npos) {
            std::cout << "[DEBUG] Found cache_config_info line (full line): " << line << std::endl;
            
            // Find num_gpu_blocks="14401" - search for the pattern more carefully
            std::string num_pattern = "num_gpu_blocks=\"";
            size_t num_start = line.find(num_pattern);
            if (num_start != std::string::npos) {
                num_start += num_pattern.length(); // Skip past "num_gpu_blocks=\""
                size_t num_end = line.find("\"", num_start);
                if (num_end != std::string::npos) {
                    std::string num_str = line.substr(num_start, num_end - num_start);
                    std::cout << "[DEBUG] Extracted num_gpu_blocks string: '" << num_str << "'" << std::endl;
                    try {
                        num_gpu_blocks = std::stoi(num_str);
                        std::cout << "[DEBUG] Successfully parsed num_gpu_blocks=" << num_gpu_blocks << std::endl;
                    } catch (const std::exception& e) {
                        std::cout << "[DEBUG] Failed to parse num_gpu_blocks from '" << num_str 
                                  << "': " << e.what() << std::endl;
                        // Try to find any number in the string
                        size_t first_digit = num_str.find_first_of("0123456789");
                        if (first_digit != std::string::npos) {
                            size_t last_digit = num_str.find_last_of("0123456789");
                            if (last_digit != std::string::npos) {
                                try {
                                    std::string cleaned = num_str.substr(first_digit, last_digit - first_digit + 1);
                                    num_gpu_blocks = std::stoi(cleaned);
                                    std::cout << "[DEBUG] Retry parsed num_gpu_blocks=" << num_gpu_blocks 
                                              << " from cleaned string: '" << cleaned << "'" << std::endl;
                                } catch (...) {}
                            }
                        }
                    }
                } else {
                    std::cout << "[DEBUG] Could not find closing quote for num_gpu_blocks" << std::endl;
                }
            } else {
                std::cout << "[DEBUG] Could not find num_gpu_blocks=\" pattern in line" << std::endl;
            }
            
            // Find block_size="16"
            std::string size_pattern = "block_size=\"";
            size_t size_start = line.find(size_pattern);
            if (size_start != std::string::npos) {
                size_start += size_pattern.length(); // Skip past "block_size=\""
                size_t size_end = line.find("\"", size_start);
                if (size_end != std::string::npos) {
                    std::string size_str = line.substr(size_start, size_end - size_start);
                    std::cout << "[DEBUG] Extracted block_size string: '" << size_str << "'" << std::endl;
                    try {
                        block_size = std::stoi(size_str);
                        std::cout << "[DEBUG] Successfully parsed block_size=" << block_size << std::endl;
                    } catch (const std::exception& e) {
                        std::cout << "[DEBUG] Failed to parse block_size from '" << size_str 
                                  << "': " << e.what() << std::endl;
                    }
                } else {
                    std::cout << "[DEBUG] Could not find closing quote for block_size" << std::endl;
                }
            } else {
                std::cout << "[DEBUG] Could not find block_size=\" pattern in line" << std::endl;
            }
        }
        
        if (line.find("kv_cache_usage_perc{") != std::string::npos) {
            size_t val_start = line.find_last_of(" ");
            if (val_start != std::string::npos) {
                try {
                    std::string usage_str = line.substr(val_start + 1);
                    kv_cache_usage = std::stod(usage_str);
                    // vLLM returns kv_cache_usage_perc as 0-1 (not 0-100), convert to percentage
                    if (kv_cache_usage > 0.0 && kv_cache_usage <= 1.0) {
                        kv_cache_usage = kv_cache_usage * 100.0;
                        std::cout << "[DEBUG] Parsed kv_cache_usage (converted from 0-1 to 0-100): " << kv_cache_usage << "%" << std::endl;
                    } else {
                        std::cout << "[DEBUG] Parsed kv_cache_usage=" << kv_cache_usage << " from string: " << usage_str << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "[DEBUG] Failed to parse kv_cache_usage: " << e.what() << std::endl;
                }
            }
        }
        
        if (line.find("num_requests_running{") != std::string::npos) {
            size_t val_start = line.find_last_of(" ");
            if (val_start != std::string::npos) {
                try {
                    num_requests_running = (int)std::stod(line.substr(val_start + 1));
                } catch (...) {}
            }
        }
        
        if (line.find("num_requests_waiting{") != std::string::npos) {
            size_t val_start = line.find_last_of(" ");
            if (val_start != std::string::npos) {
                try {
                    num_requests_waiting = (int)std::stod(line.substr(val_start + 1));
                } catch (...) {}
            }
        }
    }
    
    std::cout << "[DEBUG] parseVLLMMetrics: num_gpu_blocks=" << num_gpu_blocks 
              << ", block_size=" << block_size 
              << ", kv_cache_usage=" << kv_cache_usage 
              << ", info.used=" << info.used 
              << ", info.total=" << info.total << std::endl;
    
    if (num_gpu_blocks > 0) {
        unsigned long long block_bytes = (unsigned long long)block_size * 1024;
        
        // num_gpu_blocks = total allocated blocks (reserved for KV cache)
        int allocated_blocks = num_gpu_blocks;
        
        // Calculate utilized blocks (blocks actually being used)
        // kv_cache_usage_perc is percentage of allocated blocks that are utilized
        int utilized_blocks = 0;
        if (kv_cache_usage > 0.0) {
            // kv_cache_usage is now in 0-100 percentage range
            double utilization_ratio = kv_cache_usage / 100.0;
            utilized_blocks = (int)(num_gpu_blocks * utilization_ratio);
            // Ensure at least 1 block if there's any usage (avoid rounding to 0)
            if (utilized_blocks == 0 && kv_cache_usage > 0.0 && num_gpu_blocks > 0) {
                utilized_blocks = 1;  // At least 1 block is utilized if usage > 0
            }
            std::cout << "[DEBUG] Using kv_cache_usage: " << kv_cache_usage 
                      << "%, utilization_ratio=" << utilization_ratio
                      << ", calculated utilized_blocks=" << utilized_blocks 
                      << " out of allocated_blocks=" << allocated_blocks << std::endl;
        } else {
            // kv_cache_usage is 0 - this means no blocks are currently utilized
            // Don't use memory fallback as it's inaccurate for KV cache
            utilized_blocks = 0;
            std::cout << "[DEBUG] kv_cache_usage is 0, setting utilized_blocks=0 (no KV cache in use)" << std::endl;
            std::cout << "[DEBUG] NOTE: Not using memory fallback as it's inaccurate for KV cache utilization" << std::endl;
        }
        
        // Free blocks = allocated but not utilized
        int free_blocks = allocated_blocks - utilized_blocks;
        
        std::cout << "[DEBUG] Final block counts: allocated_blocks=" << allocated_blocks 
                  << ", utilized_blocks=" << utilized_blocks
                  << ", free_blocks=" << free_blocks << std::endl;
        
        // Try to use Nsight Compute metrics for actual block activity if available
        unsigned long long nsight_active_blocks = 0;
        for (const auto& [pid, nsight] : info.nsight_metrics) {
            if (nsight.available && nsight.active_blocks > 0) {
                nsight_active_blocks = nsight.active_blocks;
                std::cout << "[DEBUG] Found Nsight Compute active_blocks=" << nsight_active_blocks 
                          << " for PID " << pid << std::endl;
                break;  // Use first available
            }
        }
        
        // Use Nsight Compute active blocks if available, otherwise fall back to kv_cache_usage
        // Note: Nsight Compute gives GPU CUDA blocks, but we can use it as a proxy for KV cache activity
        int actual_utilized_blocks = utilized_blocks;
        if (nsight_active_blocks > 0) {
            // Scale Nsight Compute blocks to KV cache blocks if needed
            // For now, use it directly as an indicator of actual GPU activity
            if (nsight_active_blocks < (unsigned long long)num_gpu_blocks) {
                actual_utilized_blocks = (int)nsight_active_blocks;
                std::cout << "[DEBUG] Using Nsight Compute active_blocks=" << nsight_active_blocks 
                          << " instead of calculated utilized_blocks=" << utilized_blocks << std::endl;
            } else {
                // If Nsight shows more blocks than allocated, cap at allocated
                actual_utilized_blocks = num_gpu_blocks;
                std::cout << "[DEBUG] Nsight active_blocks (" << nsight_active_blocks 
                          << ") exceeds num_gpu_blocks, capping at " << num_gpu_blocks << std::endl;
            }
        }
        
        // active_blocks = utilized blocks (actually being used)
        info.active_blocks = actual_utilized_blocks;
        info.free_blocks = num_gpu_blocks - actual_utilized_blocks;
        
        // Clear existing blocks and populate with vLLM block data
        // Use Nsight Compute data when available for more accurate utilization
        info.blocks.clear();
        for (int i = 0; i < num_gpu_blocks; ++i) {
            MemoryBlock block;
            block.block_id = i;
            block.size = block_bytes;
            block.address = 0;
            block.allocated = true;  // All blocks in num_gpu_blocks are allocated/reserved
            // Use actual_utilized_blocks which may be from Nsight Compute
            block.utilized = (i < actual_utilized_blocks);
            block.type = "kv_cache";
            info.blocks.push_back(block);
        }
        
        std::cout << "[DEBUG] Populated " << num_gpu_blocks << " blocks: " 
                  << actual_utilized_blocks << " marked as utilized (from "
                  << (nsight_active_blocks > 0 ? "Nsight Compute" : "kv_cache_usage") << "), " 
                  << (num_gpu_blocks - actual_utilized_blocks) << " marked as allocated but not utilized" << std::endl;
    }
    
    for (int i = 0; i < num_requests_running; ++i) {
        ThreadInfo ti;
        ti.thread_id = info.threads.size();
        ti.allocated_bytes = 0;
        ti.state = "running";
        info.threads.push_back(ti);
    }
    
    for (int i = 0; i < num_requests_waiting; ++i) {
        ThreadInfo ti;
        ti.thread_id = info.threads.size();
        ti.allocated_bytes = 0;
        ti.state = "waiting";
        info.threads.push_back(ti);
    }
}

// Get Nsight Compute metrics for a specific PID using ncu CLI
NsightMetrics getNsightMetrics(unsigned int pid) {
    NsightMetrics metrics = {0, 0, 0, 0, 0.0, false};
    
    // Check if ncu is available
    FILE* ncu_check = popen("which ncu > /dev/null 2>&1", "r");
    if (!ncu_check) {
        return metrics;
    }
    int ncu_available = pclose(ncu_check);
    if (ncu_available != 0) {
        return metrics;  // ncu not found
    }
    
    // Use ncu to get metrics for the process
    // Note: ncu --target-processes requires the process to be running CUDA kernels
    // We'll use a lightweight query approach
    std::string cmd = absl::StrCat(
        "timeout 2 ncu --target-processes ", pid,
        " --metrics sm__sass_thread_inst_executed_op_atom_pred_on.sum,sm__thread_inst_executed.sum,sm__warps_active.avg.pct_of_peak_sustained_active",
        " --print-gpu-trace --csv 2>/dev/null | tail -20"
    );
    
    FILE* ncu_output = popen(cmd.c_str(), "r");
    if (!ncu_output) {
        return metrics;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), ncu_output)) {
        std::string line_str(line);
        // Parse ncu output for atomic operations
        if (line_str.find("sm__sass_thread_inst_executed_op_atom") != std::string::npos) {
            // Extract numeric value
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.atomic_operations = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        // Parse for threads per block
        if (line_str.find("launch__threads_per_block") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.threads_per_block = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        // Parse occupancy
        if (line_str.find("sm__warps_active") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.occupancy = std::stod(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
    }
    pclose(ncu_output);
    
    // Mark as available if we got any data
    if (metrics.atomic_operations > 0 || metrics.threads_per_block > 0) {
        metrics.available = true;
    }
    
    return metrics;
}

std::string createDetailedResponse(const DetailedVRAMInfo& info, const std::string& vllm_metrics = "") {
    std::ostringstream oss;
    double usedPercent = info.total > 0 ? (100.0 * info.used / info.total) : 0.0;
    oss << R"({"total_bytes":)" << info.total
        << R"(,"used_bytes":)" << info.used
        << R"(,"free_bytes":)" << info.free
        << R"(,"reserved_bytes":)" << info.reserved
        << R"(,"used_percent":)" << std::fixed << std::setprecision(2) << usedPercent
        << R"(,"active_blocks":)" << info.active_blocks
        << R"(,"free_blocks":)" << info.free_blocks
        << R"(,"atomic_allocations_bytes":)" << info.atomic_allocations
        << R"(,"fragmentation_ratio":)" << std::fixed << std::setprecision(4) << info.fragmentation_ratio
        << R"(,"processes":[)";
    for (size_t i = 0; i < info.processes.size(); ++i) {
        if (i > 0) oss << ",";
        oss << R"({"pid":)" << info.processes[i].pid
            << R"(,"name":")" << info.processes[i].name << R"(")"
            << R"(,"used_bytes":)" << info.processes[i].used_bytes
            << R"(,"reserved_bytes":)" << info.processes[i].reserved_bytes << "}";
    }
    oss << R"(],"threads":[)";
    for (size_t i = 0; i < info.threads.size(); ++i) {
        if (i > 0) oss << ",";
        oss << R"({"thread_id":)" << info.threads[i].thread_id
            << R"(,"allocated_bytes":)" << info.threads[i].allocated_bytes
            << R"(,"state":")" << info.threads[i].state << R"("})";
    }
    oss << R"(],"blocks":[)";
    for (size_t i = 0; i < info.blocks.size(); ++i) {
        if (i > 0) oss << ",";
        oss << R"({"block_id":)" << info.blocks[i].block_id
            << R"(,"address":)" << info.blocks[i].address
            << R"(,"size":)" << info.blocks[i].size
            << R"(,"type":")" << info.blocks[i].type << R"(")"
            << R"(,"allocated":)" << (info.blocks[i].allocated ? "true" : "false")
            << R"(,"utilized":)" << (info.blocks[i].utilized ? "true" : "false") << "}";
    }
    oss << "]";
    
    // Add Nsight Compute metrics
    oss << R"(,"nsight_metrics":{)";
    bool first_nsight = true;
    for (const auto& [pid, metrics] : info.nsight_metrics) {
        if (!first_nsight) oss << ",";
        first_nsight = false;
        oss << R"(")" << pid << R"(":{)"
            << R"("atomic_operations":)" << metrics.atomic_operations
            << R"(,"threads_per_block":)" << metrics.threads_per_block
            << R"(,"occupancy":)" << std::fixed << std::setprecision(4) << metrics.occupancy
            << R"(,"active_blocks":)" << metrics.active_blocks
            << R"(,"memory_throughput":)" << metrics.memory_throughput
            << R"(,"dram_read_bytes":)" << metrics.dram_read_bytes
            << R"(,"dram_write_bytes":)" << metrics.dram_write_bytes
            << R"(,"available":)" << (metrics.available ? "true" : "false")
            << "}";
    }
    oss << "}";
    
    if (!vllm_metrics.empty()) {
        oss << R"(,"vllm_metrics":")";
        for (char c : vllm_metrics) {
            if (c == '"' || c == '\\' || c == '\n') {
                oss << '\\';
                if (c == '\n') oss << 'n';
                else oss << c;
            } else {
                oss << c;
            }
        }
        oss << R"(")";
    }
    oss << "}";
    return oss.str();
}

void handleStreamingRequest(tcp::socket& socket) {
    http::response<http::string_body> res;
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/event-stream");
    res.set(http::field::cache_control, "no-cache");
    res.set(http::field::connection, "keep-alive");
    res.body() = "";
    res.prepare_payload();
    http::write(socket, res);
    
    while (true) {
        try {
            std::string vllm_metrics = fetchVLLMMetrics();
            DetailedVRAMInfo info = getDetailedVRAMUsage(vllm_metrics);
            std::string json = createDetailedResponse(info, vllm_metrics);
            
            std::ostringstream event;
            event << "data: " << json << "\n\n";
            
            http::response<http::string_body> chunk;
            chunk.result(http::status::ok);
            chunk.set(http::field::content_type, "text/event-stream");
            chunk.body() = event.str();
            chunk.prepare_payload();
            
            http::write(socket, chunk);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (...) {
            break;
        }
    }
}

void handleRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    std::string target = std::string(req.target());
    
    if (req.method() == http::verb::get) {
        if (target == "/vram" || target == "/vram/stream") {
            if (target == "/vram/stream") {
                handleStreamingRequest(socket);
                return;
            }
            
            std::string vllm_metrics = fetchVLLMMetrics();
            DetailedVRAMInfo info = getDetailedVRAMUsage(vllm_metrics);
            std::string json = createDetailedResponse(info, vllm_metrics);
            
            http::response<http::string_body> res;
            res.version(req.version());
            res.keep_alive(req.keep_alive());
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json;
            res.prepare_payload();
            http::write(socket, res);
            return;
        }
    }
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.body() = "Not Found";
    res.prepare_payload();
    http::write(socket, res);
}

void acceptConnections(tcp::acceptor& acceptor) {
    while (true) {
        try {
            tcp::socket socket(acceptor.get_executor());
            acceptor.accept(socket);
            
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            handleRequest(req, socket);
        } catch (const boost::system::system_error& e) {
            // Handle broken pipe and other connection errors gracefully
            if (e.code() == boost::asio::error::broken_pipe || 
                e.code() == boost::asio::error::connection_reset) {
                // Client disconnected, continue to next connection
                continue;
            }
            std::cerr << "Connection error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error handling request: " << e.what() << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        int port = 8080;
        if (argc > 1) port = std::stoi(argv[1]);

        net::io_context ioc;
        tcp::endpoint endpoint(tcp::v4(), port);
        tcp::acceptor acceptor(ioc, endpoint);
        
        std::cout << "VRAM monitor server listening on " 
                  << endpoint.address().to_string() << ":" << port << std::endl;
        acceptConnections(acceptor);
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
