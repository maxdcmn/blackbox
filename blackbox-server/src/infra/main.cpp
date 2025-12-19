#include "infra/http_server.h"
#include "services/nvml_utils.h"
#include "services/model_manager.h"
#include "utils/logger.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        int port = 6767;
        if (argc > 1) port = std::stoi(argv[1]);

        net::io_context ioc;
        tcp::endpoint endpoint(net::ip::address_v4::any(), port);
        tcp::acceptor acceptor(ioc, endpoint);
        
        std::cout << "VRAM monitor server listening on 0.0.0.0:" << port << std::endl;
        
        // Log current log level
        std::string level_str = "INFO";
        switch (Logger::getLevel()) {
            case LogLevel::DEBUG: level_str = "DEBUG"; break;
            case LogLevel::INFO:  level_str = "INFO"; break;
            case LogLevel::WARN:  level_str = "WARN"; break;
            case LogLevel::ERROR: level_str = "ERROR"; break;
        }
        LOG_INFO("Starting Blackbox Server on port " + std::to_string(port) + " (log level: " + level_str + ")");
        initNVML();
        LOG_INFO("NVML initialized successfully");
        startHealthCheckThread();
        LOG_INFO("Server ready to accept connections");
        acceptConnections(acceptor);
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        shutdownNVML();
        return 1;
    }
    shutdownNVML();
    return 0;
}
