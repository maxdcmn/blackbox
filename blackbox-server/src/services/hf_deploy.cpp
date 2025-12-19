#include "services/hf_deploy.h"
#include "services/model_manager.h"
#include "utils/env_utils.h"
#include "utils/logger.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <string>
#include <regex>
#include <absl/strings/str_cat.h>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <algorithm>
#include <cctype>
#include <iomanip>

// Helper function to trim whitespace from both ends of a string
std::string trimWhitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
}

// Helper function to URL encode a string (simple version for model IDs)
std::string urlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    
    for (unsigned char c : str) {
        // Keep alphanumeric and common safe characters (RFC 3986 unreserved)
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            encoded << static_cast<char>(c);
        } else {
            // URL encode everything else as %XX
            encoded << '%' << std::setw(2) << static_cast<unsigned int>(c);
        }
    }
    
    return encoded.str();
}

// Helper function to get Docker command prefix (with or without sudo)
std::string getDockerCmd() {
    std::string use_sudo = getEnvValue("USE_SUDO_DOCKER", "");
    if (use_sudo == "true" || use_sudo == "1" || use_sudo == "yes") {
        return "sudo docker";
    }
    
    // Try to detect if sudo is needed by checking docker access
    FILE* test_pipe = popen("docker ps >/dev/null 2>&1", "r");
    if (test_pipe) {
        int status = pclose(test_pipe);
        if (status != 0) {
            // Docker command failed, likely needs sudo
            return "sudo docker";
        }
    }
    
    return "docker";
}

// Get number of GPUs available
int getGPUCount() {
    int gpu_count = 1; // Default to 1
    
    // Try nvidia-smi first
    FILE* smi_pipe = popen("nvidia-smi -L 2>/dev/null | wc -l", "r");
    if (smi_pipe) {
        char buffer[32];
        if (fgets(buffer, sizeof(buffer), smi_pipe)) {
            try {
                gpu_count = std::stoi(buffer);
                if (gpu_count < 1) gpu_count = 1;
            } catch (...) {}
        }
        pclose(smi_pipe);
    }
    
    return gpu_count;
}

// Search for model using HuggingFace API
std::string searchHFModel(const std::string& search_term, const std::string& hf_token) {
    // Trim whitespace from search term
    std::string cleaned = trimWhitespace(search_term);
    if (cleaned.empty()) {
        LOG_ERROR("Search term is empty after trimming");
        return "";
    }
    
    // Trim whitespace from token as well
    std::string cleaned_token = trimWhitespace(hf_token);
    
    LOG_DEBUG("Searching HuggingFace for model: " + cleaned);
    
    // URL encode search term for query parameter
    std::string encoded = urlEncode(cleaned);
    
    std::string cmd = absl::StrCat(
        "curl -s --max-time 30 -H \"Authorization: Bearer ", cleaned_token, "\" ",
        "\"https://huggingface.co/api/models?search=", encoded, "&sort=downloads&direction=-1&limit=5\" 2>/dev/null"
    );
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERROR("Failed to execute model search");
        return "";
    }
    
    char buffer[8192];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    int wait_status = pclose(pipe);
    int curl_exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
    
    if (curl_exit_code != 0 || result.empty()) {
        LOG_ERROR("Model search failed (curl exit code: " + std::to_string(curl_exit_code) + ")");
        return "";
    }
    
    // Parse JSON to find the first model ID
    // Look for "id":"model/name" pattern
    size_t id_pos = result.find("\"id\":\"");
    if (id_pos != std::string::npos) {
        id_pos += 6; // Skip "id":"
        size_t end_pos = result.find("\"", id_pos);
        if (end_pos != std::string::npos) {
            std::string model_id = result.substr(id_pos, end_pos - id_pos);
            LOG_INFO("Found model: " + model_id);
            return model_id;
        }
    }
    
    return "";
}

ModelInfo validateHFModel(const std::string& model_id, const std::string& hf_token) {
    ModelInfo info;
    info.valid = false;
    info.gated = false;
    
    // Trim whitespace and hidden characters from model_id
    std::string cleaned_model_id = trimWhitespace(model_id);
    if (cleaned_model_id.empty()) {
        info.error = "Model ID is empty or contains only whitespace";
        return info;
    }
    info.id = cleaned_model_id;
    
    // Trim whitespace from token as well (tokens shouldn't have it, but be safe)
    std::string cleaned_token = trimWhitespace(hf_token);
    
    LOG_DEBUG("Validating model: " + cleaned_model_id);
    
    // URL encode the model_id for use in the URL path
    std::string encoded_model_id = urlEncode(cleaned_model_id);
    
    // First, try to get model info
    // Note: We use .c_str() implicitly via absl::StrCat, and the URL is properly quoted
    std::string cmd = absl::StrCat(
        "curl -s --max-time 30 -w \"\\nHTTP_CODE:%{http_code}\" -H \"Authorization: Bearer ", cleaned_token, "\" ",
        "\"https://huggingface.co/api/models/", encoded_model_id, "\" 2>&1"
    );
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        info.error = "Failed to connect to HuggingFace API";
        return info;
    }
    
    char buffer[8192];
    std::string result;
    std::string all_output;
    std::string http_code = "";
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        all_output += line;
        if (line.find("HTTP_CODE:") != std::string::npos) {
            // Extract HTTP status code
            size_t code_pos = line.find("HTTP_CODE:") + 10;
            http_code = line.substr(code_pos);
            // Remove newline
            http_code.erase(http_code.find_last_not_of(" \n\r\t") + 1);
        } else {
            result += line;
        }
    }
    int wait_status = pclose(pipe);
    int curl_exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
    
    // Check curl exit status first - HTTP 000 usually means connection failed
    if (curl_exit_code != 0 || http_code == "000" || http_code.empty()) {
        // Try to extract error message from output
        std::string error_msg;
        
        // Look for curl error messages
        size_t curl_error_pos = all_output.find("curl:");
        if (curl_error_pos != std::string::npos) {
            size_t error_start = curl_error_pos + 5; // Skip "curl:"
            size_t error_end = all_output.find('\n', error_start);
            if (error_end == std::string::npos) error_end = all_output.length();
            error_msg = all_output.substr(error_start, error_end - error_start);
            // Trim whitespace
            error_msg.erase(0, error_msg.find_first_not_of(" \t\n\r"));
            error_msg.erase(error_msg.find_last_not_of(" \t\n\r") + 1);
        }
        
        // If no curl error message, check for other error patterns
        if (error_msg.empty()) {
            size_t error_pos = all_output.find("error");
            if (error_pos != std::string::npos) {
                size_t line_start = all_output.rfind('\n', error_pos);
                if (line_start == std::string::npos) line_start = 0;
                size_t line_end = all_output.find('\n', error_pos);
                if (line_end == std::string::npos) line_end = all_output.length();
                error_msg = all_output.substr(line_start, line_end - line_start);
                error_msg.erase(0, error_msg.find_first_not_of(" \t\n\r"));
                error_msg.erase(error_msg.find_last_not_of(" \t\n\r") + 1);
            }
        }
        
        // Build error message
        if (!error_msg.empty()) {
            info.error = "Failed to connect to HuggingFace API: " + error_msg;
        } else if (curl_exit_code > 0) {
            // Map common curl exit codes to meaningful messages
            std::string curl_error;
            switch (curl_exit_code) {
                case 6: curl_error = "Could not resolve host"; break;
                case 7: curl_error = "Failed to connect to host"; break;
                case 28: curl_error = "Operation timeout"; break;
                case 35: curl_error = "SSL connect error"; break;
                case 60: curl_error = "SSL certificate problem"; break;
                default: curl_error = "curl error " + std::to_string(curl_exit_code);
            }
            info.error = "Failed to connect to HuggingFace API: " + curl_error;
        } else {
            info.error = "Failed to connect to HuggingFace API (network error)";
        }
        return info;
    }
    
    // If 404, try searching for the model before failing
    if (http_code == "404") {
        LOG_DEBUG("Model not found (404), attempting search");
        std::string found_id = searchHFModel(model_id, hf_token);
        if (!found_id.empty()) {
            info.id = found_id;
            return validateHFModel(found_id, hf_token);
        }
        info.error = "Model not found: " + model_id;
        return info;
    }
    
    if (http_code != "200") {
        info.error = "API request failed with HTTP " + http_code;
        return info;
    }
    
    // Check if model exists
    if (result.find("\"id\":") == std::string::npos && 
        result.find("\"modelId\":") == std::string::npos) {
        // Model not found, try searching
        LOG_DEBUG("Model not found, attempting search");
        std::string found_id = searchHFModel(model_id, hf_token);
        if (!found_id.empty()) {
            info.id = found_id;
            // Recursively validate the found model
            return validateHFModel(found_id, hf_token);
        }
        info.error = "Model not found: " + model_id;
        return info;
    }
    
    // Check if model is gated
    if (result.find("\"gated\":true") != std::string::npos ||
        result.find("\"gated\": true") != std::string::npos) {
        info.gated = true;
        LOG_DEBUG("Model is gated (requires access)");
    }
    
    // Extract the actual model ID from response
    size_t id_pos = result.find("\"id\":\"");
    if (id_pos != std::string::npos) {
        id_pos += 6;
        size_t end_pos = result.find("\"", id_pos);
        if (end_pos != std::string::npos) {
            info.id = result.substr(id_pos, end_pos - id_pos);
        }
    }
    
    info.valid = true;
    LOG_INFO("Model validated: " + info.id + (info.gated ? " (gated)" : ""));
    return info;
}

double getMaxGPUUtilizationFromConfig(const std::string& config_path) {
    try {
        YAML::Node config = YAML::LoadFile(config_path);
        
        // Try different field names (dash, underscore, old format)
        if (config["gpu-memory-utilization"]) {
            return config["gpu-memory-utilization"].as<double>();
        } else if (config["gpu_memory_utilization"]) {
            return config["gpu_memory_utilization"].as<double>();
        } else if (config["max_gpu_utilization"]) {
            return config["max_gpu_utilization"].as<double>();
        }
    } catch (const YAML::Exception& e) {
        LOG_DEBUG("Failed to parse YAML config: " + std::string(e.what()));
    } catch (...) {
        // File doesn't exist or other error
    }
    return 0.95; // Default to 95%
}

std::string getConfigPathForGPU(const std::string& gpu_type) {
    FILE* pwd_pipe = popen("pwd", "r");
    char pwd_buffer[512];
    std::string base_path = ".";
    if (pwd_pipe && fgets(pwd_buffer, sizeof(pwd_buffer), pwd_pipe)) {
        base_path = pwd_buffer;
        base_path.erase(base_path.find_last_not_of(" \t\n\r") + 1);
    }
    if (pwd_pipe) pclose(pwd_pipe);
    
    std::string config_file = base_path + "/blackbox-server/src/configs/" + gpu_type + ".yaml";
    FILE* check = fopen(config_file.c_str(), "r");
    if (check) {
        fclose(check);
        return config_file;
    }
    return base_path + "/blackbox-server/src/configs/T4.yaml";
}

std::string generateDockerCommand(const std::string& model_id, const std::string& hf_token, int port, const std::string& config_path, int tensor_parallel_size) {
    std::ostringstream cmd;
    std::string container_name = "vllm-" + std::regex_replace(model_id, std::regex("[^a-zA-Z0-9]"), "-");
    
    std::string abs_config_path = config_path;
    if (config_path.find("/") != 0) {
        FILE* pwd_pipe = popen("pwd", "r");
        char pwd_buffer[512];
        if (pwd_pipe && fgets(pwd_buffer, sizeof(pwd_buffer), pwd_pipe)) {
            std::string pwd(pwd_buffer);
            pwd.erase(pwd.find_last_not_of(" \t\n\r") + 1);
            abs_config_path = pwd + "/" + config_path;
        }
        if (pwd_pipe) pclose(pwd_pipe);
    }
    
    std::string docker_cmd = getDockerCmd();
    cmd << docker_cmd << " run -d --runtime nvidia --gpus all "
        << "-p 0.0.0.0:" << port << ":8000 "
        << "-v ~/.cache/huggingface:/root/.cache/huggingface "
        << "-v " << abs_config_path << ":/tmp/config.yaml:ro "
        << "--env \"HF_TOKEN=" << hf_token << "\" "
        << "--ipc=host "
        << "--name " << container_name << " "
        << "vllm/vllm-openai:latest "
        << "--model " << model_id
        << " --config /tmp/config.yaml"
        << " --host 0.0.0.0"
        << " --trust-remote-code";
    
    return cmd.str();
}

DeployResponse deployHFModel(const std::string& model_id, const std::string& hf_token, int port, const std::string& gpu_type, const std::string& custom_config_path) {
    DeployResponse response{false, "", "", port};
    
    if (model_id.empty()) {
        response.message = "Model ID is required";
        return response;
    }
    
    std::string token = hf_token;
    if (token.empty()) {
        token = getEnvValue("HF_TOKEN");
        if (token.empty()) {
            response.message = "HF token is required (provide in request or set HF_TOKEN in .env)";
            return response;
        }
    }
    
    if (!canDeployModel()) {
        int current = getDeployedModelCount();
        int max_allowed = getMaxConcurrentModels();
        response.message = absl::StrCat("Cannot deploy: ", current, " models already deployed (max: ", max_allowed, ")");
        return response;
    }
    
    LOG_DEBUG("Validating HF model: " + model_id);
    // Validate model using HuggingFace API
    ModelInfo model_info = validateHFModel(model_id, token);
    if (!model_info.valid) {
        LOG_ERROR("Model validation failed: " + model_info.error);
        response.message = "Model validation failed: " + model_info.error;
        if (!model_info.id.empty() && model_info.id != model_id) {
            response.message += " (Did you mean: " + model_info.id + "?)";
        }
        return response;
    }
    
    // Use the validated/corrected model ID
    std::string validated_model_id = model_info.id;
    if (validated_model_id != model_id) {
        LOG_INFO("Using corrected model ID: " + validated_model_id + " (from: " + model_id + ")");
    }
    
    if (model_info.gated) {
        LOG_DEBUG("Model is gated - ensuring token has access");
    }
    
    LOG_DEBUG("Model validation successful: " + validated_model_id);
    
    std::string container_name = getContainerName(validated_model_id);
    
    // Check if model is already deployed (early check before expensive operations)
    if (isModelDeployed(validated_model_id)) {
        LOG_WARN("Model already deployed: " + validated_model_id + " - existing container will be replaced");
    }
    
    // Check if port is already in use by another container
    std::string docker_cmd_prefix = getDockerCmd();
    std::string port_check_cmd = absl::StrCat(docker_cmd_prefix, " ps --format '{{.Names}}|{{.Ports}}' 2>/dev/null");
    FILE* port_check_pipe = popen(port_check_cmd.c_str(), "r");
    if (port_check_pipe) {
        char port_buffer[512];
        while (fgets(port_buffer, sizeof(port_buffer), port_check_pipe)) {
            std::string line(port_buffer);
            size_t pipe_pos = line.find('|');
            if (pipe_pos != std::string::npos) {
                std::string container = line.substr(0, pipe_pos);
                std::string ports = line.substr(pipe_pos + 1);
                // Check if this port is in the ports string (format: "0.0.0.0:8000->8000/tcp")
                std::string port_str = ":" + std::to_string(port);
                if (ports.find(port_str) != std::string::npos && container != container_name) {
                    container.erase(0, container.find_first_not_of(" \t\n\r"));
                    container.erase(container.find_last_not_of(" \t\n\r") + 1);
                    pclose(port_check_pipe);
                    response.message = "Port " + std::to_string(port) + " is already in use by container: " + container;
                    LOG_ERROR(response.message);
                    return response;
                }
            }
        }
        pclose(port_check_pipe);
    }
    
    std::string detected_gpu = gpu_type.empty() ? detectGPUType() : gpu_type;
    std::string config_path = custom_config_path.empty() ? getConfigPathForGPU(detected_gpu) : custom_config_path;
    double max_gpu_util = getMaxGPUUtilizationFromConfig(config_path);
    
    // Get GPU count for tensor parallelism
    int num_gpus = getGPUCount();
    int tensor_parallel_size = num_gpus;
    
    // Allow override from env
    std::string tpe_env = getEnvValue("TENSOR_PARALLEL_SIZE", "");
    if (!tpe_env.empty()) {
        try {
            tensor_parallel_size = std::stoi(tpe_env);
            if (tensor_parallel_size < 1) tensor_parallel_size = 1;
            if (tensor_parallel_size > num_gpus) tensor_parallel_size = num_gpus;
        } catch (...) {
            tensor_parallel_size = num_gpus;
        }
    }
    
    LOG_INFO("Container name: " + container_name + ", GPU: " + detected_gpu + 
             ", GPUs: " + std::to_string(num_gpus) + 
             ", Tensor Parallel: " + std::to_string(tensor_parallel_size) +
             ", Config: " + config_path);
    
    // Check if vllm image exists, pull if not
    LOG_DEBUG("Checking for vllm/vllm-openai:latest image");
    std::string image_check = absl::StrCat(docker_cmd_prefix, " images -q vllm/vllm-openai:latest 2>/dev/null");
    FILE* image_pipe = popen(image_check.c_str(), "r");
    bool image_exists = false;
    if (image_pipe) {
        char img_buffer[128];
        if (fgets(img_buffer, sizeof(img_buffer), image_pipe)) {
            std::string img_id(img_buffer);
            img_id.erase(0, img_id.find_first_not_of(" \t\n\r"));
            img_id.erase(img_id.find_last_not_of(" \t\n\r") + 1);
            image_exists = !img_id.empty();
        }
        pclose(image_pipe);
    }
    
    if (!image_exists) {
        LOG_INFO("Pulling vllm/vllm-openai:latest image (this may take a while)...");
        std::string pull_cmd = absl::StrCat(docker_cmd_prefix, " pull vllm/vllm-openai:latest 2>&1");
        FILE* pull_pipe = popen(pull_cmd.c_str(), "r");
        if (pull_pipe) {
            char pull_buffer[1024];
            while (fgets(pull_buffer, sizeof(pull_buffer), pull_pipe)) {
                // Just consume output, log important lines
                std::string line(pull_buffer);
                if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
                    LOG_WARN("Docker pull warning: " + line.substr(0, 100));
                }
            }
            int pull_status = pclose(pull_pipe);
            if (pull_status != 0) {
                LOG_ERROR("Failed to pull Docker image");
                response.message = "Failed to pull required Docker image: vllm/vllm-openai:latest";
                return response;
            }
            LOG_INFO("Docker image pulled successfully");
        }
    } else {
        LOG_DEBUG("Docker image already exists");
    }
    
    std::string check_cmd = absl::StrCat(docker_cmd_prefix, " ps -a --filter name=", container_name, " --format {{.ID}} 2>/dev/null");
    FILE* check_pipe = popen(check_cmd.c_str(), "r");
    if (check_pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), check_pipe)) {
            std::string existing_id(buffer);
            if (!existing_id.empty() && existing_id.find_first_not_of(" \t\n\r") != std::string::npos) {
                existing_id.erase(existing_id.find_last_not_of(" \t\n\r") + 1);
                std::string stop_cmd = absl::StrCat(docker_cmd_prefix, " stop ", existing_id, " 2>/dev/null");
                std::string rm_cmd = absl::StrCat(docker_cmd_prefix, " rm ", existing_id, " 2>/dev/null");
                system(stop_cmd.c_str());
                system(rm_cmd.c_str());
            }
        }
        pclose(check_pipe);
    }
    
    std::string docker_cmd = generateDockerCommand(validated_model_id, token, port, config_path, tensor_parallel_size);
    // Update docker command in the generated script to use the correct prefix
    size_t docker_pos = docker_cmd.find("docker ");
    if (docker_pos != std::string::npos && docker_cmd_prefix != "docker") {
        docker_cmd.replace(docker_pos, 7, docker_cmd_prefix + " ");
    }
    
    std::string script_path = absl::StrCat("/tmp/deploy_", container_name, ".sh");
    std::ofstream script(script_path);
    if (!script.is_open()) {
        response.message = "Failed to create deployment script";
        return response;
    }
    
    script << "#!/bin/bash\n";
    script << docker_cmd << " 2>&1\n";
    script.close();
    
    std::string chmod_cmd = absl::StrCat("chmod +x ", script_path);
    system(chmod_cmd.c_str());
    
    LOG_DEBUG("Executing deployment script: " + script_path);
    FILE* deploy_pipe = popen(absl::StrCat(script_path, " 2>&1").c_str(), "r");
    if (!deploy_pipe) {
        LOG_ERROR("Failed to execute deployment script");
        response.message = "Failed to execute deployment";
        return response;
    }
    
    char buffer[1024];
    std::string output;
    std::string stderr_output;
    while (fgets(buffer, sizeof(buffer), deploy_pipe)) {
        std::string line(buffer);
        output += line;
        // Check if this is an error line
        if (line.find("Error:") != std::string::npos || 
            line.find("error") != std::string::npos ||
            line.find("Unable") != std::string::npos) {
            stderr_output += line;
        }
    }
    int status = pclose(deploy_pipe);
    
    // Extract container ID from output
    // Docker run returns the container ID on success (64 hex chars, we need 12)
    std::string container_id;
    
    // Look for container ID in output (64 hex characters)
    // It's usually the first non-empty line that's all hex
    std::istringstream output_stream(output);
    std::string line;
    while (std::getline(output_stream, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\n\r"));
        line.erase(line.find_last_not_of(" \t\n\r") + 1);
        
        // Skip error messages and empty lines
        if (line.empty() || 
            line.find("Error") != std::string::npos ||
            line.find("error") != std::string::npos ||
            line.find("Unable") != std::string::npos ||
            line.find("template") != std::string::npos ||
            line.find("::") != std::string::npos ||
            line.find("sh:") != std::string::npos) {
            continue;
        }
        
        // Check if it looks like a container ID (all hex, at least 12 chars)
        if (line.length() >= 12) {
            bool is_hex = true;
            for (size_t i = 0; i < std::min(static_cast<size_t>(64), line.length()); i++) {
                if (!std::isxdigit(static_cast<unsigned char>(line[i]))) {
                    is_hex = false;
                    break;
                }
            }
            if (is_hex) {
                container_id = line.substr(0, 12);
                break;
            }
        }
    }
    
    if (status != 0 || container_id.empty()) {
        // Try to find container ID from container name as fallback
        std::string find_cmd = absl::StrCat(docker_cmd_prefix, " ps -a --filter name=", container_name, " --format {{.ID}} 2>/dev/null");
        FILE* find_pipe = popen(find_cmd.c_str(), "r");
        if (find_pipe) {
            char find_buffer[128];
            if (fgets(find_buffer, sizeof(find_buffer), find_pipe)) {
                std::string found_id(find_buffer);
                found_id.erase(0, found_id.find_first_not_of(" \t\n\r"));
                found_id.erase(found_id.find_last_not_of(" \t\n\r") + 1);
                if (found_id.length() >= 12) {
                    container_id = found_id.substr(0, 12);
                }
            }
            pclose(find_pipe);
        }
        
        if (container_id.empty()) {
            LOG_ERROR("Docker deployment failed. Status: " + std::to_string(status) + ", Output: " + output.substr(0, 500));
            response.message = "Deployment failed: " + (stderr_output.empty() ? output.substr(0, 200) : stderr_output.substr(0, 200));
            return response;
        } else {
            LOG_WARN("Docker command returned non-zero but container found: " + container_id);
        }
    }
    
    LOG_INFO("Docker container started: " + container_id);
    
    // Wait a moment for container to be ready, then verify it's actually running
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Check container status
    std::string status_cmd = absl::StrCat(docker_cmd_prefix, " ps --filter id=", container_id, " --format {{.Status}} 2>/dev/null");
    FILE* status_pipe = popen(status_cmd.c_str(), "r");
    if (status_pipe) {
        char status_buffer[256];
        if (fgets(status_buffer, sizeof(status_buffer), status_pipe)) {
            std::string container_status(status_buffer);
            container_status.erase(0, container_status.find_first_not_of(" \t\n\r"));
            container_status.erase(container_status.find_last_not_of(" \t\n\r") + 1);
            LOG_INFO("Container status: " + container_status);
            
            // Check if container exited immediately
            if (container_status.find("Exited") == 0 || container_status.find("Created") == 0) {
                // Get logs to see why it failed
                std::string logs_cmd = absl::StrCat(docker_cmd_prefix, " logs --tail 20 ", container_id, " 2>&1");
                FILE* logs_pipe = popen(logs_cmd.c_str(), "r");
                if (logs_pipe) {
                    char logs_buffer[2048];
                    std::string logs;
                    while (fgets(logs_buffer, sizeof(logs_buffer), logs_pipe)) {
                        logs += logs_buffer;
                    }
                    pclose(logs_pipe);
                    if (!logs.empty()) {
                        LOG_WARN("Container logs (last 20 lines):\n" + logs.substr(0, 1000));
                    }
                }
            }
        }
        pclose(status_pipe);
    }
    unsigned int pid = 0;
    std::string pid_cmd = absl::StrCat(docker_cmd_prefix, " inspect --format '{{.State.Pid}}' ", container_id, " 2>/dev/null");
    FILE* pid_pipe = popen(pid_cmd.c_str(), "r");
    if (pid_pipe) {
        char pid_buffer[32];
        if (fgets(pid_buffer, sizeof(pid_buffer), pid_pipe)) {
            try {
                std::string pid_str(pid_buffer);
                pid_str.erase(0, pid_str.find_first_not_of(" \t\n\r"));
                pid_str.erase(pid_str.find_last_not_of(" \t\n\r") + 1);
                if (!pid_str.empty() && pid_str != "0") {
                    pid = std::stoi(pid_str);
                }
            } catch (...) {
                LOG_DEBUG("Could not parse PID, will retry later");
            }
        }
        pclose(pid_pipe);
    }
    
    // If PID is 0, try again after a short delay
    if (pid == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        pid_pipe = popen(pid_cmd.c_str(), "r");
        if (pid_pipe) {
            char pid_buffer[32];
            if (fgets(pid_buffer, sizeof(pid_buffer), pid_pipe)) {
                try {
                    std::string pid_str(pid_buffer);
                    pid_str.erase(0, pid_str.find_first_not_of(" \t\n\r"));
                    pid_str.erase(pid_str.find_last_not_of(" \t\n\r") + 1);
                    if (!pid_str.empty() && pid_str != "0") {
                        pid = std::stoi(pid_str);
                    }
                } catch (...) {}
            }
            pclose(pid_pipe);
        }
    }
    
    // Final check: verify container is actually running and healthy
    // Wait a bit longer for vLLM to start (it can take 10-30 seconds for large models)
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    
    // Check multiple times to catch containers that start then fail
    bool is_running = false;
    std::string final_status;
    for (int check = 0; check < 3; check++) {
        std::string final_status_cmd = absl::StrCat(docker_cmd_prefix, " ps --filter id=", container_id, " --format {{.Status}} 2>/dev/null");
        FILE* final_status_pipe = popen(final_status_cmd.c_str(), "r");
        if (final_status_pipe) {
            char final_status_buffer[256];
            if (fgets(final_status_buffer, sizeof(final_status_buffer), final_status_pipe)) {
                final_status = final_status_buffer;
                final_status.erase(0, final_status.find_first_not_of(" \t\n\r"));
                final_status.erase(final_status.find_last_not_of(" \t\n\r") + 1);
                is_running = (final_status.find("Up") == 0);
                if (is_running) {
                    LOG_INFO("Container is running. Status: " + final_status);
                    break;
                } else {
                    LOG_DEBUG("Container check " + std::to_string(check + 1) + "/3: Status: " + final_status);
                }
            }
            pclose(final_status_pipe);
        }
        if (!is_running && check < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
    }
    
    if (!is_running) {
        LOG_WARN("Container created but not running. Final status: " + final_status);
        
        // Get detailed error information
        if (final_status.find("Exited") == 0) {
            // Get exit code
            std::string inspect_cmd = absl::StrCat(docker_cmd_prefix, " inspect --format '{{.State.ExitCode}}' ", container_id, " 2>/dev/null");
            FILE* exit_pipe = popen(inspect_cmd.c_str(), "r");
            if (exit_pipe) {
                char exit_buffer[32];
                if (fgets(exit_buffer, sizeof(exit_buffer), exit_pipe)) {
                    std::string exit_code(exit_buffer);
                    exit_code.erase(0, exit_code.find_first_not_of(" \t\n\r"));
                    exit_code.erase(exit_code.find_last_not_of(" \t\n\r") + 1);
                    LOG_ERROR("Container exited with code: " + exit_code);
                }
                pclose(exit_pipe);
            }
            
            // Get full logs to see what went wrong
            std::string logs_cmd = absl::StrCat(docker_cmd_prefix, " logs --tail 50 ", container_id, " 2>&1");
            FILE* logs_pipe = popen(logs_cmd.c_str(), "r");
            if (logs_pipe) {
                char logs_buffer[4096];
                std::string logs;
                while (fgets(logs_buffer, sizeof(logs_buffer), logs_pipe)) {
                    logs += logs_buffer;
                }
                pclose(logs_pipe);
                if (!logs.empty()) {
                    LOG_ERROR("Container logs:\n" + logs.substr(0, 2000));
                    // Try to extract the actual error message
                    size_t error_pos = logs.find("Error");
                    size_t exception_pos = logs.find("Exception");
                    size_t failed_pos = logs.find("Failed");
                    if (error_pos != std::string::npos || exception_pos != std::string::npos || failed_pos != std::string::npos) {
                        size_t start = std::min({error_pos != std::string::npos ? error_pos : logs.length(),
                                                 exception_pos != std::string::npos ? exception_pos : logs.length(),
                                                 failed_pos != std::string::npos ? failed_pos : logs.length()});
                        std::string error_snippet = logs.substr(start, 500);
                        LOG_ERROR("Error snippet: " + error_snippet);
                    }
                }
            }
        } else if (final_status.find("Created") == 0) {
            LOG_WARN("Container is in Created state - it may not have started yet");
        } else if (final_status.find("Restarting") == 0) {
            LOG_WARN("Container is restarting - may be in a crash loop");
        }
    }
    
    // Quick health check - just try once with short timeout, don't block the response
    // The container is running, so we return success immediately
    // Full health check can be done later via /models endpoint
    bool is_healthy = false;
    if (is_running) {
        LOG_DEBUG("Performing quick health check on vLLM API...");
        std::string health_cmd = absl::StrCat("timeout 2 curl -s -f -m 2 http://localhost:", port, "/health 2>/dev/null || echo 'FAILED'");
        FILE* health_pipe = popen(health_cmd.c_str(), "r");
        if (health_pipe) {
            char health_buffer[128];
            if (fgets(health_buffer, sizeof(health_buffer), health_pipe)) {
                std::string health_response(health_buffer);
                health_response.erase(0, health_response.find_first_not_of(" \t\n\r"));
                health_response.erase(health_response.find_last_not_of(" \t\n\r") + 1);
                if (health_response != "FAILED" && !health_response.empty()) {
                    is_healthy = true;
                    LOG_INFO("vLLM API health check passed immediately");
                } else {
                    LOG_DEBUG("vLLM API not ready yet (this is normal for large models)");
                }
            }
            pclose(health_pipe);
        }
    }
    
    registerModelDeployment(validated_model_id, container_name, max_gpu_util, detected_gpu, pid);
    
    response.container_id = container_id;
    if (is_running && is_healthy) {
        response.success = true;
        response.message = absl::StrCat("Model deployed successfully. Container: ", container_id, " (running and healthy)");
        LOG_INFO("Deployment successful - container is running and API is healthy");
    } else if (is_running) {
        // Container is running but API not ready yet - still report success since container is up
        // Large models can take 5-10+ minutes to load, so this is expected
        response.success = true;
        response.message = absl::StrCat("Container started: ", container_id, " on port ", port, ". API is still loading (this is normal for large models and may take 5-10+ minutes). Check status with: docker logs ", container_id);
        LOG_INFO("Container is running but API not ready yet - deployment successful, model is loading");
    } else {
        // Container failed to start - failure
        response.success = false;
        response.message = absl::StrCat("Container created: ", container_id, " but failed to start. Check logs with: docker logs ", container_id);
        LOG_ERROR("Deployment failed - container is not running");
    }
    
    return response;
}

