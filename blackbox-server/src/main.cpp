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
#include <cctype>
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
    // Note: blocks are GPU memory blocks (application-level), not GPU CUDA blocks
    std::vector<MemoryBlock> blocks;
    // Process-level VRAM usage from NVML (system-level monitoring)
    std::vector<ProcessMemory> processes;
    // Note: threads are application-level threads, not GPU CUDA threads
    // For GPU thread/block/atomic metrics, use NVIDIA Nsight Compute (NCU) profiler
    std::vector<ThreadInfo> threads;
    unsigned int active_blocks;  // GPU memory blocks (allocated)
    unsigned int utilized_blocks; // GPU memory blocks (actively used)
    unsigned int free_blocks;    // GPU memory blocks (allocated but unused)
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

NsightMetrics getNsightMetrics(unsigned int pid);

// Fetch vLLM block allocation data (minimal - only blocks)
struct VLLMBlockData {
    unsigned int num_gpu_blocks;
    unsigned long long block_size;
    double kv_cache_usage_perc;  // KV cache usage percentage (0.0-1.0)
    bool available;
};

VLLMBlockData fetchVLLMBlockData() {
    VLLMBlockData data{0, 0, 0.0, false};
    
    // Try to fetch from vLLM metrics endpoint
    FILE* curl = popen("curl -s --max-time 1 http://localhost:8000/metrics 2>/dev/null", "r");
    if (!curl) return data;
    
    char line[4096];
    while (fgets(line, sizeof(line), curl)) {
        std::string line_str(line);
        
        // Parse from vllm:cache_config_info metric (num_gpu_blocks is in labels)
        size_t pos = line_str.find("vllm:cache_config_info");
        if (pos != std::string::npos) {
            // Extract num_gpu_blocks from labels: num_gpu_blocks="14401"
            size_t num_blocks_pos = line_str.find("num_gpu_blocks=\"", pos);
            if (num_blocks_pos != std::string::npos) {
                num_blocks_pos += 16; // Skip "num_gpu_blocks=\""
                size_t num_blocks_end = line_str.find("\"", num_blocks_pos);
                if (num_blocks_end != std::string::npos) {
                    std::string value_str = line_str.substr(num_blocks_pos, num_blocks_end - num_blocks_pos);
                    // Extract only digits
                    std::string digits;
                    for (char c : value_str) {
                        if (std::isdigit(c)) digits += c;
                    }
                    if (!digits.empty()) {
                        data.num_gpu_blocks = std::stoi(digits);
                    }
                }
            }
            
            // Extract block_size from labels: block_size="16" (in tokens, need to convert to bytes)
            size_t block_size_pos = line_str.find("block_size=\"", pos);
            if (block_size_pos != std::string::npos) {
                block_size_pos += 12; // Skip "block_size=\""
                size_t block_size_end = line_str.find("\"", block_size_pos);
                if (block_size_end != std::string::npos) {
                    std::string value_str = line_str.substr(block_size_pos, block_size_end - block_size_pos);
                    std::string digits;
                    for (char c : value_str) {
                        if (std::isdigit(c)) digits += c;
                    }
                    if (!digits.empty()) {
                        // block_size="16" is tokens per block, but actual memory block size is ~16KB
                        // vLLM uses fixed-size blocks for KV cache (typically 16KB regardless of token count)
                        // We ignore the token count and use the standard vLLM block size
                        // (The token count is for logical organization, not physical memory size)
                    }
                }
            }
        }
        
        // Parse kv_cache_usage_perc from vLLM metrics (separate metric, not in cache_config_info)
        // Format: vllm:kv_cache_usage_perc{engine="0",model_name="..."} 0.0
        size_t kv_usage_pos = line_str.find("vllm:kv_cache_usage_perc");
        if (kv_usage_pos != std::string::npos) {
            // Find the value after the closing brace
            size_t brace_end = line_str.find("}", kv_usage_pos);
            if (brace_end != std::string::npos) {
                size_t value_start = line_str.find_first_not_of(" \t", brace_end + 1);
                if (value_start != std::string::npos) {
                    size_t value_end = line_str.find_first_of(" \n", value_start);
                    if (value_end != std::string::npos) {
                        std::string value_str = line_str.substr(value_start, value_end - value_start);
                        try {
                            data.kv_cache_usage_perc = std::stod(value_str);
                            // vLLM returns 0-1 range, ensure it's in that range
                            if (data.kv_cache_usage_perc < 0.0) data.kv_cache_usage_perc = 0.0;
                            if (data.kv_cache_usage_perc > 1.0) data.kv_cache_usage_perc = 1.0;
                        } catch (...) {
                            data.kv_cache_usage_perc = 0.0;
                        }
                    }
                }
            }
        }
    }
    
    pclose(curl);
    
    if (data.num_gpu_blocks > 0) {
        data.available = true;
        // Block size will be calculated from actual GPU memory usage in getDetailedVRAMUsage()
        // This is just a fallback default
        data.block_size = 16 * 1024; // 16KB fallback (will be overridden by calculation)
        std::cout << "[DEBUG] vLLM blocks: num_gpu_blocks=" << data.num_gpu_blocks 
                  << ", kv_cache_usage=" << (data.kv_cache_usage_perc * 100.0) << "%" << std::endl;
    } else {
        std::cout << "[DEBUG] vLLM block data not available" << std::endl;
    }
    
    return data;
}

// Get detailed VRAM usage from NVML (system-level)
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
DetailedVRAMInfo getDetailedVRAMUsage() {
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
            // This provides utilization data (which blocks are actively used)
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

    // Get vLLM block allocation data (only for allocated blocks) - fetch before processing Nsight metrics
    VLLMBlockData vllm_blocks = fetchVLLMBlockData();
    if (vllm_blocks.available) {
        detailed.active_blocks = vllm_blocks.num_gpu_blocks;
        // free_blocks will be calculated after we determine utilization
        
        // Calculate block size from NVML GPU memory data (not hardcoded)
        // Block size = process GPU memory / number of blocks
        // This gives actual GPU memory allocation per block from NVIDIA APIs
        unsigned long long calculated_block_size = vllm_blocks.block_size; // Default fallback
        for (const auto& pm : detailed.processes) {
            // Find vLLM process
            if (pm.name.find("python") != std::string::npos || 
                pm.name.find("vllm") != std::string::npos ||
                pm.name.find("VLLM") != std::string::npos) {
                // Calculate from actual NVML GPU memory allocation
                if (vllm_blocks.num_gpu_blocks > 0 && pm.used_bytes > 0) {
                    calculated_block_size = pm.used_bytes / vllm_blocks.num_gpu_blocks;
                    std::cout << "[DEBUG] Block size from NVML GPU memory: " 
                              << calculated_block_size << " bytes (process: " 
                              << pm.used_bytes << " bytes / " 
                              << vllm_blocks.num_gpu_blocks << " blocks)" << std::endl;
                }
                break;
            }
        }
        
        // Populate blocks array with allocated blocks
        for (unsigned int i = 0; i < vllm_blocks.num_gpu_blocks; ++i) {
            MemoryBlock block;
            block.block_id = i;
            block.address = 0; // vLLM doesn't expose addresses
            block.size = calculated_block_size; // Use calculated size from GPU memory
            block.type = "kv_cache"; // vLLM blocks are primarily KV cache
            block.allocated = true;
            block.utilized = false; // Will be set by Nsight Compute metrics below
            detailed.blocks.push_back(block);
        }
    } else {
        // No vLLM data available
        detailed.active_blocks = 0;
        detailed.utilized_blocks = 0;
        detailed.free_blocks = 0;
    }
    
    // Mark blocks as utilized based on vLLM's kv_cache_usage_perc (accurate, from vLLM's internal state)
    unsigned int utilized_count = 0;
    if (!detailed.blocks.empty() && vllm_blocks.available) {
        // Calculate actual utilized blocks from vLLM's kv_cache_usage_perc
        // This is the real utilization from vLLM's block manager, not an estimate
        unsigned int actual_utilized = static_cast<unsigned int>(
            vllm_blocks.num_gpu_blocks * vllm_blocks.kv_cache_usage_perc + 0.5  // Round to nearest
        );
        
        // Ensure we don't exceed allocated blocks
        actual_utilized = std::min(actual_utilized, detailed.active_blocks);
        
        // Mark blocks as utilized
        for (size_t j = 0; j < detailed.blocks.size() && j < actual_utilized; ++j) {
            detailed.blocks[j].utilized = true;
            utilized_count++;
        }
        
        detailed.utilized_blocks = utilized_count;
        std::cout << "[DEBUG] Block utilization: " << utilized_count << " / " << detailed.active_blocks 
                  << " blocks utilized (" << (vllm_blocks.kv_cache_usage_perc * 100.0) << "%)" << std::endl;
    } else {
        detailed.utilized_blocks = 0;
    }
    
    // Calculate free blocks: allocated but not utilized
    if (detailed.active_blocks > 0) {
        detailed.free_blocks = detailed.active_blocks - detailed.utilized_blocks;
    }

    // Set atomic allocations to sum of all process memory allocations from NVML
    // This represents the total memory allocated atomically by all processes
    detailed.atomic_allocations = total_atomic_allocations > 0 ? total_atomic_allocations : detailed.used;

    // Calculate fragmentation
    detailed.fragmentation_ratio = detailed.total > 0 ? 
        (1.0 - (double)detailed.free / detailed.total) : 0.0;

    // Threads array removed - it was just a 1:1 mapping of processes with no useful information
    // NVML doesn't provide per-thread GPU metrics, only process-level
    // For actual GPU thread/block metrics, see nsight_metrics
#endif
    return detailed;
}

// Get Nsight Compute metrics for a specific PID using ncu CLI
NsightMetrics getNsightMetrics(unsigned int pid) {
    NsightMetrics metrics{};
    metrics.atomic_operations = 0;
    metrics.threads_per_block = 0;
    metrics.occupancy = 0.0;
    metrics.active_blocks = 0;
    metrics.memory_throughput = 0;
    metrics.dram_read_bytes = 0;
    metrics.dram_write_bytes = 0;
    metrics.available = false;
    
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
        " --metrics sm__sass_thread_inst_executed_op_atom_pred_on.sum,sm__thread_inst_executed.sum,sm__warps_active.avg.pct_of_peak_sustained_active,dram__bytes_read.sum,dram__bytes_write.sum",
        " --print-gpu-trace --csv 2>/dev/null | tail -30"
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
        // Parse DRAM read bytes
        if (line_str.find("dram__bytes_read") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.dram_read_bytes = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        // Parse DRAM write bytes
        if (line_str.find("dram__bytes_write") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.dram_write_bytes = std::stoull(line_str.substr(last_space + 1));
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

std::string createDetailedResponse(const DetailedVRAMInfo& info) {
    std::ostringstream oss;
    double usedPercent = info.total > 0 ? (100.0 * info.used / info.total) : 0.0;
    oss << R"({"total_bytes":)" << info.total
        << R"(,"used_bytes":)" << info.used
        << R"(,"free_bytes":)" << info.free
        << R"(,"reserved_bytes":)" << info.reserved
        << R"(,"used_percent":)" << std::fixed << std::setprecision(2) << usedPercent
        << R"(,"active_blocks":)" << info.active_blocks
        << R"(,"utilized_blocks":)" << info.utilized_blocks
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
    oss << "}";
    return oss.str();
}

void handleStreamingRequest(tcp::socket& socket) {
    try {
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
                DetailedVRAMInfo info = getDetailedVRAMUsage();
                std::string json = createDetailedResponse(info);
                
                std::ostringstream event;
                event << "data: " << json << "\n\n";
                
                http::response<http::string_body> chunk;
                chunk.result(http::status::ok);
                chunk.set(http::field::content_type, "text/event-stream");
                chunk.body() = event.str();
                chunk.prepare_payload();
                
                http::write(socket, chunk);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (const boost::system::system_error& e) {
                // Client disconnected during streaming (handles both boost::system and boost::beast errors)
                if (e.code() == boost::asio::error::broken_pipe || 
                    e.code() == boost::asio::error::connection_reset ||
                    e.code() == boost::asio::error::eof ||
                    e.code() == boost::beast::http::error::end_of_stream) {
                    break;
                }
                throw; // Re-throw other errors
            }
        }
    } catch (...) {
        // Client disconnected, exit silently
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
            
            DetailedVRAMInfo info = getDetailedVRAMUsage();
            std::string json = createDetailedResponse(info);
            
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
            // Handle connection errors gracefully - client disconnected
            // (boost::beast::system_error is a typedef of boost::system::system_error)
            if (e.code() == boost::asio::error::broken_pipe || 
                e.code() == boost::asio::error::connection_reset ||
                e.code() == boost::asio::error::eof ||
                e.code() == boost::beast::http::error::end_of_stream) {
                // Client disconnected, continue to next connection (silent)
                continue;
            }
            // Only log non-connection errors
            std::cerr << "Connection error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            // Check if it's an end of stream message
            std::string err_msg = e.what();
            if (err_msg.find("end of stream") != std::string::npos ||
                err_msg.find("end_of_stream") != std::string::npos) {
                continue; // Client disconnected, continue silently
            }
            std::cerr << "Error handling request: " << e.what() << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        int port = 6767;
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
