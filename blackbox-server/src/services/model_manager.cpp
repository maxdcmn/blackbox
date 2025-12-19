#include "services/model_manager.h"
#include "services/vram_tracker.h"
#include "services/hf_deploy.h"
#include "utils/env_utils.h"
#include "utils/logger.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <regex>
#include <algorithm>
#include <map>
#include <set>
#include <deque>
#include <numeric>
#include <thread>
#include <chrono>
#include <absl/strings/str_cat.h>

// Helper function to get Docker command prefix (with or without sudo)
static std::string getDockerCmd() {
    std::string use_sudo = getEnvValue("USE_SUDO_DOCKER", "");
    if (use_sudo == "true" || use_sudo == "1" || use_sudo == "yes") {
        return "sudo docker";
    }
    
    // Try to detect if sudo is needed by checking docker access (with timeout)
    FILE* test_pipe = popen("timeout 2 docker ps >/dev/null 2>&1", "r");
    if (test_pipe) {
        int status = pclose(test_pipe);
        if (status != 0) {
            // Docker command failed, likely needs sudo
            return "sudo docker";
        }
    }
    
    return "docker";
}

static std::map<std::string, ModelMetrics> model_metrics;
static const int MAX_SAMPLES = 100;

int getMaxConcurrentModels() {
    std::string max_str = getEnvValue("MAX_CONCURRENT_MODELS", "3");
    try {
        int max = std::stoi(max_str);
        return max > 0 ? max : 3;
    } catch (...) {
        return 3;
    }
}

std::string getContainerName(const std::string& model_id) {
    return "vllm-" + std::regex_replace(model_id, std::regex("[^a-zA-Z0-9]"), "-");
}

std::vector<DeployedModel> listDeployedModels() {
    std::vector<DeployedModel> models;
    
    std::string docker_cmd = getDockerCmd();
    // Only query running containers - explicitly filter for running status
    std::string docker_list_cmd = absl::StrCat("timeout 5 ", docker_cmd, " ps --filter name=vllm- --filter status=running --format '{{.ID}}|{{.Names}}|{{.Status}}|{{.Ports}}' 2>/dev/null");
    FILE* pipe = popen(docker_list_cmd.c_str(), "r");
    if (!pipe) return models;
    
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        std::string line_str(line);
        if (line_str.empty()) continue;
        
        std::istringstream iss(line_str);
        std::string container_id, name, status, ports;
        
        std::getline(iss, container_id, '|');
        std::getline(iss, name, '|');
        std::getline(iss, status, '|');
        std::getline(iss, ports);
        
        if (name.find("vllm-") != 0) continue;
        
        // Trim whitespace from all fields
        container_id.erase(0, container_id.find_first_not_of(" \t\n\r"));
        container_id.erase(container_id.find_last_not_of(" \t\n\r") + 1);
        name.erase(0, name.find_first_not_of(" \t\n\r"));
        name.erase(name.find_last_not_of(" \t\n\r") + 1);
        status.erase(0, status.find_first_not_of(" \t\n\r"));
        status.erase(status.find_last_not_of(" \t\n\r") + 1);
        
        std::string model_id = name.substr(5);
        
        int port = 8000;
        // Parse port from Docker format: "0.0.0.0:8001->8000/tcp" or "8001/tcp" or ":8001->8000/tcp"
        // Look for pattern: host_port->container_port/tcp
        size_t arrow_pos = ports.find("->");
        if (arrow_pos != std::string::npos) {
            // Format: host:port->container/tcp
            size_t colon_pos = ports.rfind(":", arrow_pos);
            if (colon_pos != std::string::npos) {
                size_t port_start = colon_pos + 1;
                size_t port_end = arrow_pos;
                try {
                    port = std::stoi(ports.substr(port_start, port_end - port_start));
                } catch (...) {}
            }
        } else {
            // Fallback: look for any port number pattern
            size_t colon_pos = ports.find(":");
            if (colon_pos != std::string::npos) {
                size_t port_start = colon_pos + 1;
                size_t port_end = ports.find_first_of("/->", port_start);
                if (port_end == std::string::npos) port_end = ports.length();
                try {
                    port = std::stoi(ports.substr(port_start, port_end - port_start));
                } catch (...) {}
            }
        }
        
        // Verify the container is actually running with docker inspect
        std::string inspect_cmd = absl::StrCat("timeout 2 ", docker_cmd, " inspect --format '{{.State.Running}}' ", container_id, " 2>/dev/null");
        FILE* inspect_pipe = popen(inspect_cmd.c_str(), "r");
        bool is_actually_running = false;
        if (inspect_pipe) {
            char inspect_buffer[16];
            if (fgets(inspect_buffer, sizeof(inspect_buffer), inspect_pipe)) {
                std::string running_state(inspect_buffer);
                running_state.erase(0, running_state.find_first_not_of(" \t\n\r"));
                running_state.erase(running_state.find_last_not_of(" \t\n\r") + 1);
                is_actually_running = (running_state == "true");
            }
            pclose(inspect_pipe);
        }
        
        // Only add if actually running
        if (!is_actually_running) {
            LOG_DEBUG("Skipping non-running container: " + name + " (status: " + status + ")");
            continue;
        }
        
        DeployedModel model;
        model.model_id = model_id;
        model.container_id = container_id;
        model.container_name = name;
        model.port = port;
        model.running = true;
        
        models.push_back(model);
    }
    pclose(pipe);
    
    return models;
}

bool isModelDeployed(const std::string& model_id) {
    std::string container_name = getContainerName(model_id);
    
    std::string docker_cmd = getDockerCmd();
    std::string cmd = absl::StrCat(docker_cmd, " ps -a --filter name=", container_name, " --format {{.ID}}");
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;
    
    char buffer[128];
    bool exists = false;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        std::string id(buffer);
        id.erase(id.find_last_not_of(" \t\n\r") + 1);
        exists = !id.empty() && id.length() >= 12;
    }
    pclose(pipe);
    
    return exists;
}

int getDeployedModelCount() {
    return listDeployedModels().size();
}

int getNextAvailablePort(int preferred_port) {
    std::vector<DeployedModel> models = listDeployedModels();
    std::set<int> used_ports;
    
    for (const auto& model : models) {
        used_ports.insert(model.port);
    }
    
    // If preferred port is provided and available, use it
    if (preferred_port > 0 && used_ports.find(preferred_port) == used_ports.end()) {
        return preferred_port;
    }
    
    // Find next available port starting from 8000
    int start_port = 8000;
    std::string start_port_env = getEnvValue("START_PORT", "");
    if (!start_port_env.empty()) {
        try {
            start_port = std::stoi(start_port_env);
        } catch (...) {}
    }
    
    int port = start_port;
    int max_port = start_port + 1000; // Safety limit
    while (port < max_port) {
        if (used_ports.find(port) == used_ports.end()) {
            return port;
        }
        port++;
    }
    
    // Fallback: return start_port even if in use (will fail at Docker level)
    return start_port;
}

bool canDeployModel() {
    int current = getDeployedModelCount();
    int max_allowed = getMaxConcurrentModels();
    return current < max_allowed;
}

bool spindownModel(const std::string& model_id_or_container) {
    std::string container_name = model_id_or_container;
    
    if (container_name.find("vllm-") != 0) {
        container_name = getContainerName(model_id_or_container);
    }
    
    unregisterModel(container_name);
    
    std::string docker_cmd = getDockerCmd();
    
    std::string stop_cmd = absl::StrCat(docker_cmd, " stop ", container_name, " 2>/dev/null");
    std::string rm_cmd = absl::StrCat(docker_cmd, " rm ", container_name, " 2>/dev/null");
    
    int stop_result = system(stop_cmd.c_str());
    int rm_result = system(rm_cmd.c_str());
    
    return (stop_result == 0 || rm_result == 0);
}

std::string detectGPUType() {
    FILE* pipe = popen("nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1", "r");
    if (!pipe) return "T4";
    
    char buffer[256];
    std::string gpu_name;
    if (fgets(buffer, sizeof(buffer), pipe)) {
        gpu_name = buffer;
        gpu_name.erase(gpu_name.find_last_not_of(" \t\n\r") + 1);
    }
    pclose(pipe);
    
    if (gpu_name.find("A100") != std::string::npos) return "A100";
    if (gpu_name.find("H100") != std::string::npos) return "H100";
    if (gpu_name.find("L40") != std::string::npos) return "L40";
    if (gpu_name.find("T4") != std::string::npos) return "T4";
    
    return "T4";
}

void registerModelDeployment(const std::string& model_id, const std::string& container_name,
                            double configured_max_gpu_utilization, const std::string& gpu_type, unsigned int pid) {
    ModelMetrics metrics;
    metrics.configured_max_utilization = configured_max_gpu_utilization;
    metrics.gpu_type = gpu_type;
    metrics.pid = pid;
    metrics.peak_usage = 0.0;
    model_metrics[container_name] = metrics;
}

void unregisterModel(const std::string& container_name) {
    model_metrics.erase(container_name);
}

// Clean up model_metrics for containers that no longer exist or aren't running
static void cleanupStaleModelMetrics() {
    auto running_models = listDeployedModels();
    std::set<std::string> running_container_names;
    for (const auto& model : running_models) {
        running_container_names.insert(model.container_name);
    }
    
    // Remove metrics for containers that are no longer running
    auto it = model_metrics.begin();
    while (it != model_metrics.end()) {
        if (running_container_names.find(it->first) == running_container_names.end()) {
            LOG_DEBUG("Removing stale metrics for non-running container: " + it->first);
            it = model_metrics.erase(it);
        } else {
            ++it;
        }
    }
}

void updateModelVRAMUsage(const std::string& container_name, double vram_percent) {
    // Clean up stale metrics before updating
    cleanupStaleModelMetrics();
    
    if (model_metrics.find(container_name) == model_metrics.end()) return;
    
    auto& metrics = model_metrics[container_name];
    metrics.vram_samples.push_back(vram_percent);
    if (metrics.vram_samples.size() > MAX_SAMPLES) {
        metrics.vram_samples.pop_front();
    }
    
    if (vram_percent > metrics.peak_usage) {
        metrics.peak_usage = vram_percent;
    }
}


OptimizationResult optimizeModelAllocations() {
    OptimizationResult result{false, {}, ""};
    std::vector<std::string> to_restart;
    
    // Clean up stale metrics first
    cleanupStaleModelMetrics();
    
    // Now iterate only over running models
    for (const auto& [container_name, metrics] : model_metrics) {
        if (metrics.vram_samples.size() < 10) continue;
        
        double avg = std::accumulate(metrics.vram_samples.begin(), metrics.vram_samples.end(), 0.0) / metrics.vram_samples.size();
        double threshold = metrics.configured_max_utilization * 100.0 * 0.7;
        
        if (avg < threshold && metrics.peak_usage > 0) {
            to_restart.push_back(container_name);
        }
    }
    
    if (to_restart.empty()) {
        result.message = "No models need optimization";
        return result;
    }
    
    result.optimized = true;
    result.restarted_models = to_restart;
    result.message = absl::StrCat("Optimizing ", to_restart.size(), " model(s)");
    
    return result;
}

void checkVLLMHealth() {
    // Clean up stale metrics first
    cleanupStaleModelMetrics();
    
    auto models = listDeployedModels();
    if (models.empty()) {
        return;
    }
    
    for (const auto& model : models) {
        
        // Check /health endpoint
        std::string health_cmd = absl::StrCat(
            "timeout 2 curl -s -w '\\nHTTP_CODE:%{http_code}' -m 1 http://localhost:", model.port, "/health 2>&1"
        );
        
        FILE* health_pipe = popen(health_cmd.c_str(), "r");
        if (health_pipe) {
            char buffer[512];
            std::string output;
            std::string http_code;
            while (fgets(buffer, sizeof(buffer), health_pipe)) {
                std::string line(buffer);
                if (line.find("HTTP_CODE:") != std::string::npos) {
                    size_t pos = line.find("HTTP_CODE:") + 10;
                    http_code = line.substr(pos);
                    http_code.erase(http_code.find_last_not_of(" \t\n\r") + 1);
                } else {
                    output += line;
                }
            }
            pclose(health_pipe);
            
            if (http_code == "200") {
                LOG_DEBUG("vLLM health check OK: " + model.model_id + " on port " + std::to_string(model.port));
            } else {
                LOG_WARN("vLLM health check failed: " + model.model_id + " on port " + std::to_string(model.port) + " (HTTP " + http_code + ")");
            }
        } else {
            LOG_WARN("Failed to execute health check for " + model.model_id);
        }
    }
}

void startHealthCheckThread() {
    std::thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            try {
                checkVLLMHealth();
            } catch (const std::exception& e) {
                LOG_ERROR("Health check error: " + std::string(e.what()));
            }
        }
    }).detach();
    LOG_INFO("Started vLLM health check thread (every 5 seconds)");
}

