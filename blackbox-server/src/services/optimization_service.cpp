#include "services/optimization_service.h"
#include "services/model_manager.h"
#include "services/hf_deploy.h"
#include "services/vram_tracker.h"
#include "utils/env_utils.h"
#include "utils/logger.h"
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

void handleOptimizeRequest(http::request<http::string_body>& req, tcp::socket& socket) {
    OptimizationResult opt_result = optimizeModelAllocations();
    
    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(req.keep_alive());
    res.set(http::field::content_type, "application/json");
    
    if (!opt_result.optimized) {
        res.result(http::status::ok);
        res.body() = R"({"success":true,"optimized":false,"message":")" + opt_result.message + R"("})";
        res.prepare_payload();
        http::write(socket, res);
        return;
    }
    
    std::vector<std::string> restarted;
    for (const auto& container_name : opt_result.restarted_models) {
        auto models = listDeployedModels();
        std::string model_id;
        std::string gpu_type;
        double peak_usage = 0.0;
        
        for (const auto& m : models) {
            if (m.container_name == container_name) {
                model_id = m.model_id;
                gpu_type = m.gpu_type;
                peak_usage = m.peak_vram_usage_percent / 100.0;
                break;
            }
        }
        
        if (model_id.empty()) continue;
        
        spindownModel(container_name);
        
        std::string hf_token = getEnvValue("HF_TOKEN");
        if (gpu_type.empty()) gpu_type = detectGPUType();
        std::string config_path = getConfigPathForGPU(gpu_type);
        
        if (peak_usage < 0.1) peak_usage = 0.1;
        if (peak_usage > 0.95) peak_usage = 0.95;
        
        std::string temp_config = "/tmp/optimized_" + container_name + ".yaml";
        try {
            // Load existing config
            YAML::Node config = YAML::LoadFile(config_path);
            
            // Update gpu-memory-utilization
            config["gpu-memory-utilization"] = peak_usage;
            
            // Write updated config with proper YAML formatting
            std::ofstream dst(temp_config);
            YAML::Emitter emitter;
            emitter << config;
            dst << emitter.c_str();
            dst.close();
        } catch (const YAML::Exception& e) {
            LOG_ERROR("Failed to parse or update YAML config: " + std::string(e.what()));
            // Fallback: copy original file
            std::ifstream src(config_path);
            std::ofstream dst(temp_config);
            dst << src.rdbuf();
            src.close();
            dst.close();
        }
        
        DeployResponse deploy_res = deployHFModel(model_id, hf_token, 8000, gpu_type, temp_config);
        if (deploy_res.success) {
            restarted.push_back(container_name);
        }
    }
    
    nlohmann::json response_json;
    response_json["success"] = true;
    response_json["optimized"] = true;
    response_json["message"] = "Optimized " + std::to_string(restarted.size()) + " model(s)";
    response_json["restarted_models"] = restarted;
    
    res.result(http::status::ok);
    res.body() = response_json.dump();
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

