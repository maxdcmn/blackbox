#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
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

DetailedVRAMInfo getDetailedVRAMUsage(const std::string& vllm_url = "") {
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

std::string createDetailedResponse(const DetailedVRAMInfo& info) {
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
    oss << "]}";
    return oss.str();
}

void handleRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());

    if (req.method() == http::verb::get && req.target() == "/vram") {
        VRAMInfo info = getVRAMUsage();
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = createResponse(info);
    } else {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        res.body() = "Not Found";
    }

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
