#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <nvml.h>
#include <absl/strings/str_format.h>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct VRAMInfo {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
};

VRAMInfo getVRAMUsage() {
    VRAMInfo info = {0, 0, 0};
#ifdef NVML_AVAILABLE
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        return info;
    }

    unsigned int deviceCount;
    if (nvmlDeviceGetCount(&deviceCount) == NVML_SUCCESS && deviceCount > 0) {
        nvmlDevice_t device;
        if (nvmlDeviceGetHandleByIndex(0, &device) == NVML_SUCCESS) {
            nvmlMemory_t memory;
            if (nvmlDeviceGetMemoryInfo(device, &memory) == NVML_SUCCESS) {
                info.total = memory.total;
                info.used = memory.used;
                info.free = memory.free;
            }
        }
    }
    nvmlShutdown();
#endif
    return info;
}

std::string createResponse(const VRAMInfo& info) {
    return absl::StrFormat(
        R"({"total_bytes":%llu,"used_bytes":%llu,"free_bytes":%llu,"used_percent":%.2f})",
        info.total, info.used, info.free,
        info.total > 0 ? (100.0 * info.used / info.total) : 0.0
    );
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
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), port));
        
        std::cout << "VRAM monitor server listening on port " << port << std::endl;
        acceptConnections(acceptor);
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
