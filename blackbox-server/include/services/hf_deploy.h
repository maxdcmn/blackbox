#pragma once

#include <string>

struct DeployResponse {
    bool success;
    std::string message;
    std::string container_id;
    int port;
};

struct ModelInfo {
    std::string id;
    bool gated;
    bool valid;
    std::string error;
};

DeployResponse deployHFModel(const std::string& model_id, const std::string& hf_token = "", int port = 8000, const std::string& gpu_type = "", const std::string& custom_config_path = "");
ModelInfo validateHFModel(const std::string& model_id, const std::string& hf_token);
std::string searchHFModel(const std::string& search_term, const std::string& hf_token);
std::string generateDockerCommand(const std::string& model_id, const std::string& hf_token, int port, const std::string& config_path, int tensor_parallel_size = 1);
int getGPUCount();
double getMaxGPUUtilizationFromConfig(const std::string& config_path);
std::string getConfigPathForGPU(const std::string& gpu_type);

