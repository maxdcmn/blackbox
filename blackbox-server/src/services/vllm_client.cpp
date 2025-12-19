#include "services/vllm_client.h"
#include "services/model_manager.h"
#include "utils/env_utils.h"
#include "utils/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <string>
#include <sstream>

VLLMBlockData fetchVLLMBlockData() {
    VLLMBlockData data{0, 0, 0.0, 0.0, false};
    
    // Get all deployed models and filter to only running ones
    auto all_models = listDeployedModels();
    std::vector<DeployedModel> models;
    for (const auto& model : all_models) {
        if (model.running) {
            models.push_back(model);
        }
    }
    
    unsigned long long total_blocks = 0;
    unsigned long long total_block_size = 0;
    double total_kv_usage = 0.0;
    double total_prefix_hit_rate = 0.0;
    int active_models = 0;
    
    for (const auto& model : models) {
        
        // Use timeout wrapper to ensure curl doesn't hang
        // Try to get host from environment, default to localhost
        std::string vllm_host = getEnvValue("VLLM_HOST", "localhost");
        std::ostringstream cmd;
        cmd << "timeout 2 bash -c 'curl -s --max-time 1.5 --connect-timeout 1.0 http://" 
            << vllm_host << ":" << model.port << "/metrics 2>/dev/null || echo'";
        
        FILE* curl = popen(cmd.str().c_str(), "r");
        if (!curl) continue;
        
        char line[4096];
        unsigned long long model_blocks = 0;
        double model_kv_usage = 0.0;
        double model_prefix_hit_rate = 0.0;
        unsigned long long cache_query_total = 0;
        unsigned long long cache_query_hit = 0;
        unsigned int requests_running = 0;
        unsigned int requests_waiting = 0;
        
        while (fgets(line, sizeof(line), curl)) {
            std::string line_str(line);
            
            size_t pos = line_str.find("vllm:cache_config_info");
            if (pos != std::string::npos) {
                size_t num_blocks_pos = line_str.find("num_gpu_blocks=\"", pos);
                if (num_blocks_pos != std::string::npos) {
                    num_blocks_pos += 16;
                    size_t num_blocks_end = line_str.find("\"", num_blocks_pos);
                    if (num_blocks_end != std::string::npos) {
                        std::string value_str = line_str.substr(num_blocks_pos, num_blocks_end - num_blocks_pos);
                        std::string digits;
                        for (char c : value_str) {
                            if (std::isdigit(c)) digits += c;
                        }
                        if (!digits.empty()) {
                            model_blocks = std::stoull(digits);
                        }
                    }
                }
            }
            
            // Parse kv_cache_usage_perc - format: vllm:kv_cache_usage_perc{...} value
            size_t kv_usage_pos = line_str.find("vllm:kv_cache_usage_perc");
            if (kv_usage_pos != std::string::npos && line_str[0] != '#') {
                size_t brace_end = line_str.find("}", kv_usage_pos);
                if (brace_end != std::string::npos) {
                    size_t value_start = line_str.find_first_not_of(" \t", brace_end + 1);
                    if (value_start != std::string::npos) {
                        size_t value_end = line_str.find_first_of(" \n\r", value_start);
                        if (value_end == std::string::npos) {
                            value_end = line_str.length();
                        }
                        if (value_end > value_start) {
                            std::string value_str = line_str.substr(value_start, value_end - value_start);
                            try {
                                model_kv_usage = std::stod(value_str);
                                if (model_kv_usage < 0.0) model_kv_usage = 0.0;
                                if (model_kv_usage > 1.0) model_kv_usage = 1.0;
                            } catch (...) {
                                model_kv_usage = 0.0;
                            }
                        }
                    }
                }
            }
            
            // Parse prefix_cache_queries_total - format: vllm:prefix_cache_queries_total{...} value
            if (line_str.find("vllm:prefix_cache_queries_total") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    // Trim whitespace
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            cache_query_total = std::stoull(value_str);
                        } catch (...) {
                            cache_query_total = 0;
                        }
                    }
                }
            }
            
            // Parse prefix_cache_hits_total - format: vllm:prefix_cache_hits_total{...} value
            if (line_str.find("vllm:prefix_cache_hits_total") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    // Trim whitespace
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            cache_query_hit = std::stoull(value_str);
                        } catch (...) {
                            cache_query_hit = 0;
                        }
                    }
                }
            }
            
            // Parse num_requests_running - format: vllm:num_requests_running{...} value
            if (line_str.find("vllm:num_requests_running") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            double val = std::stod(value_str);
                            requests_running = static_cast<unsigned int>(val);
                        } catch (...) {
                            requests_running = 0;
                        }
                    }
                }
            }
            
            // Parse num_requests_waiting - format: vllm:num_requests_waiting{...} value
            if (line_str.find("vllm:num_requests_waiting") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            double val = std::stod(value_str);
                            requests_waiting = static_cast<unsigned int>(val);
                        } catch (...) {
                            requests_waiting = 0;
                        }
                    }
                }
            }
        }
        
        pclose(curl);
        
        // Calculate hit rate from counters
        if (cache_query_total > 0) {
            model_prefix_hit_rate = (double(cache_query_hit) / double(cache_query_total)) * 100.0;
            if (model_prefix_hit_rate < 0.0) model_prefix_hit_rate = 0.0;
            if (model_prefix_hit_rate > 100.0) model_prefix_hit_rate = 100.0;
        }
        
        // Aggregate data from this model
        if (model_blocks > 0) {
            total_blocks += model_blocks;
            total_kv_usage += model_kv_usage;
            total_prefix_hit_rate += model_prefix_hit_rate;
            active_models++;
            
            // Estimate block size (16KB is typical)
            if (total_block_size == 0) {
                total_block_size = 16 * 1024; // Default 16KB per block
            }
        }
    }
    
    // Set aggregated data
    if (total_blocks > 0 && active_models > 0) {
        data.num_gpu_blocks = total_blocks;
        data.block_size = total_block_size;
        data.kv_cache_usage_perc = total_kv_usage / active_models; // Average across models
        data.prefix_cache_hit_rate = total_prefix_hit_rate / active_models; // Average across models
        data.available = true;
    }
    
    return data;
}

std::vector<ModelBlockData> fetchPerModelBlockData() {
    std::vector<ModelBlockData> models_data;
    
    // Get all deployed models and filter to only running ones
    auto all_models = listDeployedModels();
    std::vector<DeployedModel> models;
    for (const auto& model : all_models) {
        if (model.running) {
            models.push_back(model);
        }
    }
    LOG_DEBUG("fetchPerModelBlockData: Found " + std::to_string(all_models.size()) + " total models, " + std::to_string(models.size()) + " running");
    
    for (const auto& model : models) {
        LOG_DEBUG("Fetching metrics for model " + model.model_id + " on port " + std::to_string(model.port));
        
        ModelBlockData model_data;
        model_data.model_id = model.model_id;
        model_data.port = model.port;
        model_data.num_gpu_blocks = 0;
        model_data.block_size = 0;
        model_data.kv_cache_usage_perc = 0.0;
        model_data.prefix_cache_hit_rate = 0.0;
        model_data.num_requests_running = 0;
        model_data.num_requests_waiting = 0;
        model_data.available = false;
        
        // Use timeout wrapper to ensure curl doesn't hang
        // Try to get host from environment, default to localhost
        std::string vllm_host = getEnvValue("VLLM_HOST", "localhost");
        std::ostringstream cmd;
        cmd << "timeout 2 bash -c 'curl -s --max-time 1.5 --connect-timeout 1.0 http://" 
            << vllm_host << ":" << model.port << "/metrics 2>/dev/null || echo'";
        
        FILE* curl = popen(cmd.str().c_str(), "r");
        if (!curl) {
            LOG_DEBUG("Failed to open curl pipe for model " + model.model_id);
            models_data.push_back(model_data);
            continue;
        }
        
        LOG_DEBUG("Fetching metrics from http://" + vllm_host + ":" + std::to_string(model.port) + "/metrics");
        char line[4096];
        int line_count = 0;
        unsigned long long model_blocks = 0;
        double model_kv_usage = 0.0;
        double model_prefix_hit_rate = 0.0;
        unsigned long long cache_query_total = 0;
        unsigned long long cache_query_hit = 0;
        unsigned int requests_running = 0;
        unsigned int requests_waiting = 0;
        bool found_cache_config = false;
        
        while (fgets(line, sizeof(line), curl)) {
            line_count++;
            std::string line_str(line);
            
            size_t pos = line_str.find("vllm:cache_config_info");
            if (pos != std::string::npos) {
                found_cache_config = true;
                size_t num_blocks_pos = line_str.find("num_gpu_blocks=\"", pos);
                if (num_blocks_pos != std::string::npos) {
                    num_blocks_pos += 16;
                    size_t num_blocks_end = line_str.find("\"", num_blocks_pos);
                    if (num_blocks_end != std::string::npos) {
                        std::string value_str = line_str.substr(num_blocks_pos, num_blocks_end - num_blocks_pos);
                        std::string digits;
                        for (char c : value_str) {
                            if (std::isdigit(c)) digits += c;
                        }
                        if (!digits.empty()) {
                            model_blocks = std::stoull(digits);
                        }
                    }
                }
            }
            
            // Parse kv_cache_usage_perc - format: vllm:kv_cache_usage_perc{...} value
            size_t kv_usage_pos = line_str.find("vllm:kv_cache_usage_perc");
            if (kv_usage_pos != std::string::npos && line_str[0] != '#') {
                size_t brace_end = line_str.find("}", kv_usage_pos);
                if (brace_end != std::string::npos) {
                    size_t value_start = line_str.find_first_not_of(" \t", brace_end + 1);
                    if (value_start != std::string::npos) {
                        size_t value_end = line_str.find_first_of(" \n\r", value_start);
                        if (value_end == std::string::npos) {
                            value_end = line_str.length();
                        }
                        if (value_end > value_start) {
                            std::string value_str = line_str.substr(value_start, value_end - value_start);
                            try {
                                model_kv_usage = std::stod(value_str);
                                if (model_kv_usage < 0.0) model_kv_usage = 0.0;
                                if (model_kv_usage > 1.0) model_kv_usage = 1.0;
                            } catch (...) {
                                model_kv_usage = 0.0;
                            }
                        }
                    }
                }
            }
            
            // Parse prefix_cache_queries_total - format: vllm:prefix_cache_queries_total{...} value
            if (line_str.find("vllm:prefix_cache_queries_total") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    // Trim whitespace
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            cache_query_total = std::stoull(value_str);
                        } catch (...) {
                            cache_query_total = 0;
                        }
                    }
                }
            }
            
            // Parse prefix_cache_hits_total - format: vllm:prefix_cache_hits_total{...} value
            if (line_str.find("vllm:prefix_cache_hits_total") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    // Trim whitespace
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            cache_query_hit = std::stoull(value_str);
                        } catch (...) {
                            cache_query_hit = 0;
                        }
                    }
                }
            }
            
            // Parse num_requests_running - format: vllm:num_requests_running{...} value
            if (line_str.find("vllm:num_requests_running") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            double val = std::stod(value_str);
                            requests_running = static_cast<unsigned int>(val);
                        } catch (...) {
                            requests_running = 0;
                        }
                    }
                }
            }
            
            // Parse num_requests_waiting - format: vllm:num_requests_waiting{...} value
            if (line_str.find("vllm:num_requests_waiting") != std::string::npos && line_str[0] != '#') {
                size_t brace = line_str.find_last_of('}');
                if (brace != std::string::npos && brace + 1 < line_str.length()) {
                    std::string value_str = line_str.substr(brace + 1);
                    size_t start = value_str.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        size_t end = value_str.find_last_not_of(" \t\r\n");
                        value_str = value_str.substr(start, end - start + 1);
                        try {
                            double val = std::stod(value_str);
                            requests_waiting = static_cast<unsigned int>(val);
                        } catch (...) {
                            requests_waiting = 0;
                        }
                    }
                }
            }
        }
        
        int curl_status = pclose(curl);
        LOG_DEBUG("Model " + model.model_id + ": curl returned " + std::to_string(curl_status) + 
                 ", read " + std::to_string(line_count) + " lines" +
                 ", found_cache_config=" + (found_cache_config ? "true" : "false") +
                 ", model_blocks=" + std::to_string(model_blocks) +
                 ", kv_usage=" + std::to_string(model_kv_usage));
        
        // Calculate hit rate from counters
        if (cache_query_total > 0) {
            model_prefix_hit_rate = (double(cache_query_hit) / double(cache_query_total)) * 100.0;
            if (model_prefix_hit_rate < 0.0) model_prefix_hit_rate = 0.0;
            if (model_prefix_hit_rate > 100.0) model_prefix_hit_rate = 100.0;
        }
        
        // Set model data
        if (model_blocks > 0) {
            model_data.num_gpu_blocks = model_blocks;
            model_data.block_size = 16 * 1024; // Default 16KB per block
            model_data.kv_cache_usage_perc = model_kv_usage;
            model_data.prefix_cache_hit_rate = model_prefix_hit_rate;
            model_data.num_requests_running = requests_running;
            model_data.num_requests_waiting = requests_waiting;
            model_data.available = true;
            LOG_DEBUG("Model " + model.model_id + " metrics: blocks=" + std::to_string(model_blocks) +
                     ", kv_usage=" + std::to_string(model_kv_usage) +
                     ", prefix_hit_rate=" + std::to_string(model_prefix_hit_rate));
        } else {
            LOG_DEBUG("Model " + model.model_id + " has 0 blocks (line_count=" + std::to_string(line_count) + 
                     "), marking as unavailable");
        }
        
        models_data.push_back(model_data);
    }
    
    return models_data;
}
