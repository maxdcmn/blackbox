#include "services/nvml_utils.h"
#include "services/vllm_client.h"
#include "services/nsight_utils.h"
#include "services/model_manager.h"
#include "utils/logger.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <map>
#include <sstream>
#include <absl/strings/str_cat.h>
#ifdef NVML_AVAILABLE
#include <nvml.h>
#endif

#ifdef NVML_AVAILABLE
static nvmlDevice_t g_device = nullptr;
#else
static void* g_device = nullptr;
#endif
static bool g_nvml_initialized = false;

bool initNVML() {
    if (g_nvml_initialized) return true;
#ifdef NVML_AVAILABLE
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        std::cerr << "[NVML] Initialization failed (error code: " << result << ")" << std::endl;
        if (result == NVML_ERROR_DRIVER_NOT_LOADED) {
            std::cerr << "[NVML] Driver not loaded. Try: sudo modprobe nvidia" << std::endl;
        } else if (result == NVML_ERROR_LIBRARY_NOT_FOUND) {
            std::cerr << "[NVML] Library not found. Install: sudo apt install -y nvidia-utils-535" << std::endl;
        } else if (result == NVML_ERROR_NO_PERMISSION) {
            std::cerr << "[NVML] Permission denied. Try running as root or add user to video group" << std::endl;
        } else {
            std::cerr << "[NVML] Check: 1) NVIDIA drivers installed? 2) GPU present? 3) nvidia-smi works?" << std::endl;
            std::cerr << "[NVML] If 'Driver/library version mismatch': Reboot or reinstall drivers" << std::endl;
        }
        return false;
    }
    
    unsigned int deviceCount = 0;
    result = nvmlDeviceGetCount(&deviceCount);
    if (result != NVML_SUCCESS) {
        std::cerr << "[NVML] Failed to get device count (error code: " << result << ")" << std::endl;
        nvmlShutdown();
        return false;
    }
    
    if (deviceCount == 0) {
        std::cerr << "[NVML] No GPU devices found" << std::endl;
        nvmlShutdown();
        return false;
    }
    
    std::cout << "[NVML] Found " << deviceCount << " GPU device(s)" << std::endl;
    
    result = nvmlDeviceGetHandleByIndex(0, &g_device);
    if (result != NVML_SUCCESS) {
        std::cerr << "[NVML] Failed to get device handle (error code: " << result << ")" << std::endl;
        nvmlShutdown();
        return false;
    }
    
    g_nvml_initialized = true;
    std::cout << "[NVML] Initialized successfully" << std::endl;
    return true;
#else
    std::cerr << "[NVML] NVML not available (compiled without NVML support)" << std::endl;
    std::cerr << "[NVML] Install: sudo apt install -y libnvidia-ml-dev" << std::endl;
    return false;
#endif
}

void shutdownNVML() {
    if (g_nvml_initialized) {
#ifdef NVML_AVAILABLE
        nvmlShutdown();
#endif
        g_nvml_initialized = false;
        g_device = nullptr;
    }
}

DetailedVRAMInfo getDetailedVRAMUsage() {
    DetailedVRAMInfo detailed = {0, 0, 0, 0, {}, {}, {}, 0, 0, 0, 0ULL, 0.0, {}, 0ULL, 0.0, {}};
    if (!initNVML()) {
        return detailed;
    }
#ifdef NVML_AVAILABLE
    nvmlMemory_t memory;
    if (nvmlDeviceGetMemoryInfo(g_device, &memory) == NVML_SUCCESS) {
        detailed.total = memory.total;
        detailed.used = memory.used;
        detailed.free = memory.free;
        detailed.reserved = memory.used;
    }

    unsigned int processCount = 64;
    nvmlProcessInfo_t processes[64];
    unsigned long long total_atomic_allocations = 0;
    
    if (nvmlDeviceGetComputeRunningProcesses(g_device, &processCount, processes) == NVML_SUCCESS) {
        for (unsigned int i = 0; i < processCount; ++i) {
            ProcessMemory pm;
            pm.pid = processes[i].pid;
            pm.used_bytes = processes[i].usedGpuMemory;
            pm.reserved_bytes = processes[i].usedGpuMemory;
            
            total_atomic_allocations += processes[i].usedGpuMemory;
            
            char name[256] = {0};
            FILE* fp = fopen(absl::StrCat("/proc/", pm.pid, "/comm").c_str(), "r");
            if (fp) {
                if (fgets(name, sizeof(name), fp)) {
                    pm.name = name;
                    if (!pm.name.empty() && pm.name.back() == '\n') {
                        pm.name.pop_back();
                    }
                } else {
                    pm.name = "unknown";
                }
                fclose(fp);
            } else {
                pm.name = "unknown";
            }
            detailed.processes.push_back(pm);
            
            // Only try to get nsight metrics for vLLM/python processes to avoid hanging
            // Skip nsight metrics collection if it might be slow
            if (pm.name.find("python") != std::string::npos || 
                pm.name.find("vllm") != std::string::npos ||
                pm.name.find("VLLM") != std::string::npos) {
                // Only collect for first few processes to avoid hanging
                if (detailed.processes.size() <= 3) {
                    NsightMetrics nsight = getNsightMetrics(pm.pid);
                    if (nsight.available) {
                        detailed.nsight_metrics[pm.pid] = nsight;
                    }
                }
            }
        }
    }

    // Fetch per-model block data
    std::vector<ModelBlockData> models_data = fetchPerModelBlockData();
    
    unsigned int total_allocated_blocks = 0;
    unsigned int total_utilized_blocks = 0;
    
    // Get deployed models to match processes to containers (only running ones)
    auto all_deployed_models = listDeployedModels();
    std::vector<DeployedModel> deployed_models;
    for (const auto& model : all_deployed_models) {
        if (model.running) {
            deployed_models.push_back(model);
        }
    }
    
    // Create a map of model_id -> process memory for block size calculation
    // Match processes to models by checking which container they belong to
    std::map<std::string, unsigned long long> model_memory;
    for (const auto& pm : detailed.processes) {
        if (pm.name.find("python") != std::string::npos || 
            pm.name.find("vllm") != std::string::npos ||
            pm.name.find("VLLM") != std::string::npos) {
            // Check process's cgroup to find container
            std::ostringstream cgroup_cmd;
            cgroup_cmd << "cat /proc/" << pm.pid << "/cgroup 2>/dev/null | grep docker";
            FILE* cgroup_pipe = popen(cgroup_cmd.str().c_str(), "r");
            if (cgroup_pipe) {
                char cgroup_line[512];
                if (fgets(cgroup_line, sizeof(cgroup_line), cgroup_pipe)) {
                    std::string cgroup(cgroup_line);
                    // Extract container ID from cgroup path
                    size_t docker_pos = cgroup.find("/docker/");
                    if (docker_pos != std::string::npos) {
                        size_t container_start = docker_pos + 8;
                        size_t container_end = cgroup.find("/", container_start);
                        if (container_end == std::string::npos) {
                            container_end = cgroup.find("\n", container_start);
                        }
                        if (container_end != std::string::npos) {
                            std::string container_id = cgroup.substr(container_start, container_end - container_start);
                            // Find which model this container belongs to
                            for (const auto& deployed : deployed_models) {
                                if (deployed.container_id.find(container_id) == 0 || container_id.find(deployed.container_id) == 0) {
                                    // Match found - use the model_id from models_data that matches
                                    for (const auto& model_data : models_data) {
                                        if (model_data.model_id == deployed.model_id) {
                                            // Sum up memory for all processes in this model
                                            model_memory[model_data.model_id] += pm.used_bytes;
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                pclose(cgroup_pipe);
            }
        }
    }
    
    // Create blocks for each model and calculate used KV cache bytes
    unsigned long long total_used_kv_cache_bytes = 0;
    for (const auto& model_data : models_data) {
        // Always include models, even if metrics aren't available yet
        ModelVRAMInfo model_info;
        model_info.model_id = model_data.model_id;
        model_info.port = model_data.port;
        model_info.allocated_vram_bytes = 0;
        model_info.used_kv_cache_bytes = 0;
        
        LOG_DEBUG("Processing model " + model_data.model_id + ": available=" + (model_data.available ? "true" : "false") + 
                 ", num_gpu_blocks=" + std::to_string(model_data.num_gpu_blocks) +
                 ", port=" + std::to_string(model_data.port));
        
        if (model_data.available && model_data.num_gpu_blocks > 0) {
            // Calculate block size for this model
            unsigned long long calculated_block_size = model_data.block_size;
            unsigned long long model_allocated_vram = 0;
            if (model_memory.find(model_data.model_id) != model_memory.end()) {
                model_allocated_vram = model_memory[model_data.model_id];
                if (model_data.num_gpu_blocks > 0 && model_allocated_vram > 0) {
                    calculated_block_size = model_allocated_vram / model_data.num_gpu_blocks;
                }
            }
            
            // If we don't have allocated VRAM, use a default block size (16KB is typical)
            if (calculated_block_size == 0) {
                calculated_block_size = 16 * 1024; // 16KB default
            }
            
            // Calculate utilized blocks for this model
            unsigned int model_utilized = static_cast<unsigned int>(
                model_data.num_gpu_blocks * model_data.kv_cache_usage_perc + 0.5
            );
            model_utilized = std::min(model_utilized, model_data.num_gpu_blocks);
            
            // Calculate used KV cache bytes for this model: num_blocks * block_size * kv_cache_usage_perc
            unsigned long long model_used_kv_bytes = static_cast<unsigned long long>(
                model_data.num_gpu_blocks * calculated_block_size * model_data.kv_cache_usage_perc
            );
            
            LOG_DEBUG("Model " + model_data.model_id + ": num_blocks=" + std::to_string(model_data.num_gpu_blocks) +
                     ", block_size=" + std::to_string(calculated_block_size) +
                     ", kv_cache_usage_perc=" + std::to_string(model_data.kv_cache_usage_perc) +
                     ", calculated_used_kv_bytes=" + std::to_string(model_used_kv_bytes) +
                     ", model_allocated_vram=" + std::to_string(model_allocated_vram));
            
            // Ensure used KV cache never exceeds allocated VRAM
            if (model_allocated_vram > 0) {
                model_used_kv_bytes = std::min(model_used_kv_bytes, model_allocated_vram);
                LOG_DEBUG("Model " + model_data.model_id + ": capped used_kv_bytes to " + std::to_string(model_used_kv_bytes));
            }
            
            total_used_kv_cache_bytes += model_used_kv_bytes;
            
            model_info.allocated_vram_bytes = model_allocated_vram;
            model_info.used_kv_cache_bytes = model_used_kv_bytes;
            
            LOG_DEBUG("Model " + model_data.model_id + ": final allocated_vram_bytes=" + std::to_string(model_info.allocated_vram_bytes) +
                     ", used_kv_cache_bytes=" + std::to_string(model_info.used_kv_cache_bytes));
            
            // Create blocks for this model
            for (unsigned int i = 0; i < model_data.num_gpu_blocks; ++i) {
                MemoryBlock block;
                block.block_id = i;
                block.address = 0;
                block.size = calculated_block_size;
                block.type = "kv_cache";
                block.allocated = true;
                block.utilized = (i < model_utilized);
                block.model_id = model_data.model_id;
                block.port = model_data.port;
                detailed.blocks.push_back(block);
            }
            
            total_allocated_blocks += model_data.num_gpu_blocks;
            total_utilized_blocks += model_utilized;
        }
        
        detailed.models.push_back(model_info);
    }
    
    detailed.allocated_blocks = total_allocated_blocks;
    detailed.utilized_blocks = total_utilized_blocks;
    detailed.free_blocks = total_allocated_blocks - total_utilized_blocks;
    
    if (detailed.allocated_blocks > 0) {
        detailed.free_blocks = detailed.allocated_blocks - detailed.utilized_blocks;
    }

    detailed.atomic_allocations = total_atomic_allocations > 0 ? total_atomic_allocations : detailed.used;

    detailed.fragmentation_ratio = detailed.total > 0 ? 
        (1.0 - (double)detailed.free / detailed.total) : 0.0;
    
    detailed.used_kv_cache_bytes = total_used_kv_cache_bytes;
    LOG_DEBUG("Total used_kv_cache_bytes: " + std::to_string(total_used_kv_cache_bytes) + ", total_allocated_blocks: " + std::to_string(total_allocated_blocks));
    
    // Calculate average prefix cache hit rate from all models
    double total_prefix_hit_rate = 0.0;
    int models_with_prefix_data = 0;
    for (const auto& model_data : models_data) {
        if (model_data.available && model_data.prefix_cache_hit_rate > 0.0) {
            total_prefix_hit_rate += model_data.prefix_cache_hit_rate;
            models_with_prefix_data++;
        }
    }
    detailed.prefix_cache_hit_rate = models_with_prefix_data > 0 ? 
        (total_prefix_hit_rate / models_with_prefix_data) : 0.0;
    
    // If we couldn't match processes to models, distribute total allocated VRAM proportionally
    // based on KV cache usage or number of models
    if (detailed.used > 0) {
        unsigned long long total_allocated_vram_matched = 0;
        for (const auto& model : detailed.models) {
            total_allocated_vram_matched += model.allocated_vram_bytes;
        }
        
        // If we matched less than 50% of total VRAM, distribute the rest
        if (total_allocated_vram_matched < detailed.used * 0.5) {
            unsigned long long remaining_vram = detailed.used - total_allocated_vram_matched;
            
            if (total_used_kv_cache_bytes > 0) {
                // Distribute proportionally based on KV cache usage
                for (auto& model : detailed.models) {
                    if (model.used_kv_cache_bytes > 0) {
                        double proportion = (double)model.used_kv_cache_bytes / (double)total_used_kv_cache_bytes;
                        model.allocated_vram_bytes += static_cast<unsigned long long>(remaining_vram * proportion);
                    }
                }
            } else if (!detailed.models.empty()) {
                // If no KV cache data, distribute evenly among models
                unsigned long long per_model = remaining_vram / detailed.models.size();
                LOG_DEBUG("Distributing " + std::to_string(remaining_vram) + " bytes evenly among " + std::to_string(detailed.models.size()) + " models (" + std::to_string(per_model) + " per model)");
                for (auto& model : detailed.models) {
                    model.allocated_vram_bytes += per_model;
                    LOG_DEBUG("Model " + model.model_id + ": allocated_vram_bytes now " + std::to_string(model.allocated_vram_bytes) + ", used_kv_cache_bytes=" + std::to_string(model.used_kv_cache_bytes));
                }
            }
        }
    }
#endif
    return detailed;
}

