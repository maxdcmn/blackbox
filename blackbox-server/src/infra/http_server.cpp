#include "infra/http_server.h"
#include "services/nvml_utils.h"
#include "services/aggregation_service.h"
#include "utils/json_serializer.h"
#include "utils/logger.h"
#include "services/deploy_service.h"
#include "services/spindown_service.h"
#include "services/optimization_service.h"
#include "services/model_manager.h"
#include "services/vram_tracker.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <string>
#include <regex>

void handleStreamingRequest(tcp::socket& socket) {
    LOG_DEBUG("handleStreamingRequest: Entering function");
    try {
        LOG_DEBUG("handleStreamingRequest: Creating response");
        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/event-stream");
        res.set(http::field::cache_control, "no-cache");
        res.set(http::field::connection, "keep-alive");
        res.body() = "";
        res.prepare_payload();
        
        LOG_DEBUG("handleStreamingRequest: Sending SSE headers");
        http::write(socket, res);
        LOG_DEBUG("handleStreamingRequest: SSE headers sent, starting stream loop");
        
        int iteration = 0;
        while (true) {
            iteration++;
            LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Starting");
            try {
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Getting VRAM info");
                DetailedVRAMInfo info = getDetailedVRAMUsage();
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Got VRAM info, getting models");
                
                auto models = listDeployedModels();
                for (const auto& model : models) {
                    if (model.running && model.pid > 0) {
                        double vram_percent = getModelVRAMUsagePercent(model.container_name, model.pid);
                        updateModelVRAMUsage(model.container_name, vram_percent);
                    }
                }
                
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Creating JSON response");
                std::string json = createDetailedResponse(info);
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": JSON created (" + std::to_string(json.length()) + " bytes)");
                
                std::ostringstream event;
                event << "data: " << json << "\n\n";
                
                http::response<http::string_body> chunk;
                chunk.result(http::status::ok);
                chunk.set(http::field::content_type, "text/event-stream");
                chunk.body() = event.str();
                chunk.prepare_payload();
                
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Writing SSE chunk (" + std::to_string(chunk.body().length()) + " bytes)");
                http::write(socket, chunk);
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": SSE chunk written, sleeping");
                
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (const boost::system::system_error& e) {
                auto ec = e.code();
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Caught system_error: " + std::string(e.what()) + " (code: " + std::to_string(ec.value()) + ")");
                if (ec == boost::asio::error::broken_pipe || 
                    ec == boost::asio::error::connection_reset ||
                    ec == boost::asio::error::eof ||
                    ec == boost::beast::http::error::end_of_stream ||
                    ec == boost::asio::error::operation_aborted ||
                    ec.category() == boost::asio::error::get_system_category()) {
                    LOG_DEBUG("Stream connection closed by client at iteration " + std::to_string(iteration));
                    break;
                }
                LOG_ERROR("Unexpected system error in stream iteration " + std::to_string(iteration) + ": " + std::string(e.what()));
                break;
            } catch (const std::exception& e) {
                std::string err_msg = e.what();
                LOG_DEBUG("Stream iteration " + std::to_string(iteration) + ": Caught exception: " + err_msg);
                if (err_msg.find("end of stream") != std::string::npos ||
                    err_msg.find("end_of_stream") != std::string::npos ||
                    err_msg.find("Broken pipe") != std::string::npos ||
                    err_msg.find("Connection reset") != std::string::npos) {
                    LOG_DEBUG("Stream connection closed at iteration " + std::to_string(iteration));
                    break;
                }
                LOG_ERROR("Error in stream iteration " + std::to_string(iteration) + ": " + err_msg);
                break;
            } catch (...) {
                LOG_ERROR("Stream iteration " + std::to_string(iteration) + ": Caught unknown exception");
                break;
            }
        }
        LOG_DEBUG("Stream loop ended after " + std::to_string(iteration) + " iterations");
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error in handleStreamingRequest: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown fatal error in handleStreamingRequest");
    }
}


void handleRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    std::string target = std::string(req.target());
    std::string method = std::string(to_string(req.method()));
    
    std::string client_ip = "unknown";
    try {
        client_ip = socket.remote_endpoint().address().to_string();
    } catch (...) {
        // Connection may be closed, use unknown
    }
    
    LOG_INFO("[" + method + "] " + target + " from " + client_ip);
    
    if (req.method() == http::verb::get) {
        if (target == "/vram" || target == "/vram/stream") {
            if (target == "/vram/stream") {
                LOG_DEBUG("Starting streaming request from " + client_ip);
                handleStreamingRequest(socket);
                LOG_DEBUG("Streaming request ended from " + client_ip);
                return;
            }
            
            LOG_DEBUG("Fetching VRAM info");
            DetailedVRAMInfo info = getDetailedVRAMUsage();
            std::string json = createDetailedResponse(info);
            
            http::response<http::string_body> res;
            res.version(req.version());
            res.keep_alive(req.keep_alive());
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json;
            res.prepare_payload();
            
            try {
                http::write(socket, res);
                LOG_DEBUG("VRAM response sent (" + std::to_string(json.length()) + " bytes)");
            } catch (const boost::system::system_error& e) {
                auto ec = e.code();
                if (ec == boost::asio::error::broken_pipe || 
                    ec == boost::asio::error::connection_reset ||
                    ec == boost::asio::error::eof) {
                    LOG_DEBUG("Client disconnected during VRAM response");
                    return;
                }
                throw;
            }
            return;
        } else if (target.find("/vram/aggregated") == 0) {
            unsigned int window_seconds = 5;
            std::regex window_regex(R"(window=(\d+))");
            std::smatch match;
            std::string query = std::string(req.target());
            size_t query_pos = query.find('?');
            if (query_pos != std::string::npos) {
                std::string query_str = query.substr(query_pos + 1);
                if (std::regex_search(query_str, match, window_regex)) {
                    try {
                        window_seconds = std::stoi(match[1].str());
                        if (window_seconds < 1) window_seconds = 1;
                        if (window_seconds > 60) window_seconds = 60;
                    } catch (...) {
                        window_seconds = 5;
                    }
                }
            }
            
            LOG_DEBUG("Collecting aggregated metrics for " + std::to_string(window_seconds) + " seconds");
            AggregatedVRAMInfo info = collectAggregatedMetrics(window_seconds);
            std::string json = createAggregatedResponse(info);
            
            http::response<http::string_body> res;
            res.version(req.version());
            res.keep_alive(req.keep_alive());
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = json;
            res.prepare_payload();
            
            try {
                http::write(socket, res);
                LOG_DEBUG("Aggregated VRAM response sent (" + std::to_string(json.length()) + " bytes)");
            } catch (const boost::system::system_error& e) {
                auto ec = e.code();
                if (ec == boost::asio::error::broken_pipe || 
                    ec == boost::asio::error::connection_reset ||
                    ec == boost::asio::error::eof) {
                    LOG_DEBUG("Client disconnected during aggregated VRAM response");
                    return;
                }
                throw;
            }
            return;
        } else if (target == "/models") {
            LOG_DEBUG("Listing deployed models");
            handleListModelsRequest(req, socket);
            return;
        }
    } else if (req.method() == http::verb::post) {
        if (target == "/deploy") {
            LOG_INFO("Deploy request received from " + client_ip);
            LOG_DEBUG("Request body: " + req.body().substr(0, 200) + (req.body().length() > 200 ? "..." : ""));
            handleDeployRequest(req, socket);
            return;
        } else if (target == "/spindown") {
            LOG_INFO("Spindown request received from " + client_ip);
            LOG_DEBUG("Request body: " + req.body());
            handleSpindownRequest(req, socket);
            return;
        } else if (target == "/optimize") {
            LOG_INFO("Optimize request received from " + client_ip);
            handleOptimizeRequest(req, socket);
            return;
        }
    }
    
    LOG_WARN("404 Not Found: " + method + " " + target + " from " + client_ip);
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.body() = "Not Found";
    res.prepare_payload();
    
    try {
        http::write(socket, res);
    } catch (const boost::system::system_error& e) {
        auto ec = e.code();
        if (ec == boost::asio::error::broken_pipe || 
            ec == boost::asio::error::connection_reset ||
            ec == boost::asio::error::eof) {
            return;
        }
        throw;
    }
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
            auto ec = e.code();
            if (ec == boost::asio::error::broken_pipe || 
                ec == boost::asio::error::connection_reset ||
                ec == boost::asio::error::eof ||
                ec == boost::beast::http::error::end_of_stream ||
                ec == boost::asio::error::operation_aborted ||
                ec.category() == boost::asio::error::get_system_category()) {
                continue;
            }
            std::cerr << "Unexpected connection error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::string err_msg = e.what();
            if (err_msg.find("end of stream") != std::string::npos ||
                err_msg.find("end_of_stream") != std::string::npos ||
                err_msg.find("Broken pipe") != std::string::npos ||
                err_msg.find("Connection reset") != std::string::npos ||
                err_msg.find("Connection refused") != std::string::npos) {
                continue;
            }
            std::cerr << "Error handling request: " << e.what() << std::endl;
        } catch (...) {
            continue;
        }
    }
}

