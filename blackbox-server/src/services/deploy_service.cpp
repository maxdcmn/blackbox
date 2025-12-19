#include "services/deploy_service.h"
#include "utils/json_parser.h"
#include "utils/env_utils.h"
#include "utils/logger.h"
#include "services/hf_deploy.h"
#include "services/model_manager.h"
#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <string>

void handleDeployRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    std::string body = req.body();
    std::string model_id_raw = parseJSONField(body, "model_id");
    std::string hf_token = parseJSONField(body, "hf_token");
    int requested_port = parseJSONInt(body, "port", 0); // 0 means auto-assign
    
    // Trim whitespace and hidden characters from model_id early
    std::string model_id = model_id_raw;
    model_id.erase(0, model_id.find_first_not_of(" \t\n\r\f\v"));
    model_id.erase(model_id.find_last_not_of(" \t\n\r\f\v") + 1);
    
    // Auto-assign port if not specified or if specified port is in use
    int port = getNextAvailablePort(requested_port);
    if (requested_port > 0 && port != requested_port) {
        LOG_WARN("Requested port " + std::to_string(requested_port) + " is in use, using port " + std::to_string(port) + " instead");
    } else if (requested_port == 0) {
        LOG_INFO("No port specified, auto-assigning port " + std::to_string(port));
    }
    
    LOG_INFO("Deploy request - model_id: " + model_id + ", port: " + std::to_string(port));
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.set(http::field::content_type, "application/json");
    
    if (model_id.empty()) {
        LOG_WARN("Deploy request rejected: model_id is required (was: \"" + model_id_raw + "\")");
        res.result(http::status::bad_request);
        res.body() = R"({"success":false,"message":"model_id is required or contains only whitespace"})";
        res.prepare_payload();
        http::write(socket, res);
        return;
    }
    
    if (hf_token.empty()) {
        hf_token = getEnvValue("HF_TOKEN");
        if (hf_token.empty()) {
            LOG_WARN("Deploy request rejected: HF_TOKEN not provided and not in .env");
            res.result(http::status::bad_request);
            res.body() = R"json({"success":false,"message":"hf_token is required (provide in request or set HF_TOKEN in .env)"})json";
            res.prepare_payload();
            http::write(socket, res);
            return;
        }
        LOG_DEBUG("Using HF_TOKEN from .env");
    }
    
    std::string gpu_type = getEnvValue("GPU_TYPE", "");
    LOG_INFO("Deploying model: " + model_id + " on port " + std::to_string(port) + (gpu_type.empty() ? "" : " (GPU: " + gpu_type + ")"));
    
    DeployResponse deploy_result = deployHFModel(model_id, hf_token, port, gpu_type);
    
    // Always return 200 OK - the JSON success field indicates actual status
    // This allows clients to parse the response even if deployment partially succeeded
    res.result(http::status::ok);
    
    // Build JSON response using nlohmann/json
    nlohmann::json response_json;
    response_json["success"] = deploy_result.success;
    response_json["message"] = deploy_result.message;
    response_json["container_id"] = deploy_result.container_id;
    response_json["port"] = deploy_result.port;
    
    res.body() = response_json.dump();
    
    if (deploy_result.success) {
        LOG_INFO("Deploy successful - container_id: " + deploy_result.container_id + ", port: " + std::to_string(deploy_result.port));
    } else {
        LOG_ERROR("Deploy failed: " + deploy_result.message);
    }
    
    res.prepare_payload();
    try {
        http::write(socket, res);
    } catch (const boost::system::system_error& e) {
        auto ec = e.code();
        if (ec == boost::asio::error::broken_pipe || 
            ec == boost::asio::error::connection_reset ||
            ec == boost::asio::error::eof) {
            LOG_DEBUG("Client disconnected during deploy response");
            return;
        }
        throw;
    }
}

