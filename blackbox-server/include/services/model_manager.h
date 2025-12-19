#pragma once

#include <string>
#include <vector>
#include <map>
#include <deque>

struct DeployedModel {
    std::string model_id;
    std::string container_id;
    std::string container_name;
    int port;
    bool running;
    double configured_max_gpu_utilization;
    double avg_vram_usage_percent;
    double peak_vram_usage_percent;
    std::string gpu_type;
    unsigned int pid;
};

struct ModelMetrics {
    std::deque<double> vram_samples;
    double peak_usage;
    double configured_max_utilization;
    std::string gpu_type;
    unsigned int pid;
};

struct ModelListResponse {
    std::vector<DeployedModel> models;
    int total;
    int running;
    int max_allowed;
};

struct OptimizationResult {
    bool optimized;
    std::vector<std::string> restarted_models;
    std::string message;
};

int getMaxConcurrentModels();
int getDeployedModelCount();
std::vector<DeployedModel> listDeployedModels();
bool isModelDeployed(const std::string& model_id);
bool canDeployModel();
int getNextAvailablePort(int preferred_port = 0);
std::string getContainerName(const std::string& model_id);
bool spindownModel(const std::string& model_id_or_container);
void updateModelVRAMUsage(const std::string& container_name, double vram_percent);
void registerModelDeployment(const std::string& model_id, const std::string& container_name, 
                             double configured_max_gpu_utilization, const std::string& gpu_type, unsigned int pid);
void unregisterModel(const std::string& container_name);
std::string detectGPUType();
OptimizationResult optimizeModelAllocations();
void checkVLLMHealth();
void startHealthCheckThread();

