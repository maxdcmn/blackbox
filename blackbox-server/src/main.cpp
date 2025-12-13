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
    bool allocated;
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

struct DetailedVRAMInfo {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
    unsigned long long reserved;
    std::vector<MemoryBlock> blocks;
    std::vector<ProcessMemory> processes;
    std::vector<ThreadInfo> threads;
    unsigned int active_blocks;
    unsigned int free_blocks;
    unsigned long long atomic_allocations;
    double fragmentation_ratio;
};

struct VRAMInfo {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
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

VRAMInfo getVRAMUsage() {
    VRAMInfo info = {0, 0, 0};
    if (!initNVML()) return info;
#ifdef NVML_AVAILABLE
    nvmlMemory_t memory;
    if (nvmlDeviceGetMemoryInfo(g_device, &memory) == NVML_SUCCESS) {
        info.total = memory.total;
        info.used = memory.used;
        info.free = memory.free;
    }
#endif
    return info;
}

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
    if (nvmlDeviceGetComputeRunningProcesses(g_device, &processCount, processes) == NVML_SUCCESS) {
        for (unsigned int i = 0; i < processCount; ++i) {
            ProcessMemory pm;
            pm.pid = processes[i].pid;
            pm.used_bytes = processes[i].usedGpuMemory;
            pm.reserved_bytes = processes[i].usedGpuMemory;
            
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
        }
    }

    // Default values (will be overridden by vLLM if available)
    detailed.active_blocks = detailed.processes.size();
    detailed.free_blocks = 0;
    detailed.atomic_allocations = detailed.used;
    detailed.fragmentation_ratio = detailed.total > 0 ? 
        (1.0 - (double)detailed.free / detailed.total) : 0.0;

    for (size_t i = 0; i < detailed.processes.size(); ++i) {
        ThreadInfo ti;
        ti.thread_id = i;
        ti.allocated_bytes = detailed.processes[i].used_bytes;
        ti.state = "active";
        detailed.threads.push_back(ti);
    }
    
    // Parse vLLM metrics to populate blocks and update counts
    if (!vllm_metrics.empty()) {
        parseVLLMMetrics(vllm_metrics, detailed);
    }
#endif
    return detailed;
}

std::string createResponse(const VRAMInfo& info) {
    std::ostringstream oss;
    double usedPercent = info.total > 0 ? (100.0 * info.used / info.total) : 0.0;
    oss << R"({"total_bytes":)" << info.total
        << R"(,"used_bytes":)" << info.used
        << R"(,"free_bytes":)" << info.free
        << R"(,"used_percent":)" << std::fixed << std::setprecision(2) << usedPercent
        << "}";
    return oss.str();
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
    if (metrics.empty()) return;
    
    std::istringstream iss(metrics);
    std::string line;
    int num_gpu_blocks = 0;
    int block_size = 16;
    double kv_cache_usage = 0.0;
    int num_requests_running = 0;
    int num_requests_waiting = 0;
    
    while (std::getline(iss, line)) {
        if (line.find("cache_config_info") != std::string::npos) {
            size_t num_start = line.find("num_gpu_blocks=\"");
            if (num_start != std::string::npos) {
                num_start += 15;
                size_t num_end = line.find("\"", num_start);
                if (num_end != std::string::npos) {
                    try {
                        num_gpu_blocks = std::stoi(line.substr(num_start, num_end - num_start));
                    } catch (...) {}
                }
            }
            
            size_t size_start = line.find("block_size=\"");
            if (size_start != std::string::npos) {
                size_start += 12;
                size_t size_end = line.find("\"", size_start);
                if (size_end != std::string::npos) {
                    try {
                        block_size = std::stoi(line.substr(size_start, size_end - size_start));
                    } catch (...) {}
                }
            }
        }
        
        if (line.find("kv_cache_usage_perc{") != std::string::npos) {
            size_t val_start = line.find_last_of(" ");
            if (val_start != std::string::npos) {
                try {
                    kv_cache_usage = std::stod(line.substr(val_start + 1));
                } catch (...) {}
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
    
    if (num_gpu_blocks > 0) {
        unsigned long long block_bytes = (unsigned long long)block_size * 1024;
        
        // Calculate active blocks based on kv_cache_usage or fallback to memory usage
        int active_blocks;
        if (kv_cache_usage > 0.0) {
            active_blocks = (int)(num_gpu_blocks * kv_cache_usage / 100.0);
        } else {
            // Fallback: estimate based on used memory vs total blocks
            // Assume blocks are used proportionally to memory usage
            if (info.total > 0) {
                double mem_usage_ratio = (double)info.used / info.total;
                active_blocks = (int)(num_gpu_blocks * mem_usage_ratio);
            } else {
                active_blocks = 0;
            }
        }
        int free_blocks = num_gpu_blocks - active_blocks;
        
        // Override with vLLM block data
        info.active_blocks = active_blocks;
        info.free_blocks = free_blocks;
        
        // Clear existing blocks and populate with vLLM block data
        info.blocks.clear();
        for (int i = 0; i < num_gpu_blocks; ++i) {
            MemoryBlock block;
            block.block_id = i;
            block.size = block_bytes;
            block.address = 0;
            block.allocated = (i < active_blocks);
            block.type = "kv_cache";
            info.blocks.push_back(block);
        }
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
            << R"(,"allocated":)" << (info.blocks[i].allocated ? "true" : "false") << "}";
    }
    oss << "]";
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
        tcp::socket socket(acceptor.get_executor());
        acceptor.accept(socket);
        
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);
        handleRequest(req, socket);
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
