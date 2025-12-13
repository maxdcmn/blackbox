#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>

#ifdef NVML_AVAILABLE
#include <nvml.h>
#endif

struct ProcessInfo {
    unsigned int pid;
    unsigned long long memory;
    std::string name;
};

std::vector<ProcessInfo> getGPUProcesses() {
    std::vector<ProcessInfo> processes;
    
#ifdef NVML_AVAILABLE
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        std::cerr << "Failed to initialize NVML: " << nvmlErrorString(result) << std::endl;
        return processes;
    }
    
    nvmlDevice_t device;
    result = nvmlDeviceGetHandleByIndex(0, &device);
    if (result != NVML_SUCCESS) {
        std::cerr << "Failed to get device handle: " << nvmlErrorString(result) << std::endl;
        nvmlShutdown();
        return processes;
    }
    
    unsigned int processCount = 64;
    nvmlProcessInfo_t processInfos[64];
    result = nvmlDeviceGetComputeRunningProcesses(device, &processCount, processInfos);
    
    if (result == NVML_SUCCESS) {
        for (unsigned int i = 0; i < processCount; ++i) {
            ProcessInfo pi;
            pi.pid = processInfos[i].pid;
            pi.memory = processInfos[i].usedGpuMemory;
            
            // Get process name
            char name[256] = {0};
            std::string proc_path = "/proc/" + std::to_string(pi.pid) + "/comm";
            std::ifstream proc_file(proc_path);
            if (proc_file.is_open()) {
                proc_file.getline(name, sizeof(name));
                pi.name = name;
                proc_file.close();
            } else {
                pi.name = "unknown";
            }
            
            processes.push_back(pi);
        }
    }
    
    nvmlShutdown();
#endif
    
    return processes;
}

bool testNsightCompute(unsigned int pid) {
    std::cout << "\n=== Testing Nsight Compute (ncu) for PID " << pid << " ===" << std::endl;
    
    // Check if ncu is available
    FILE* check = popen("which ncu 2>/dev/null", "r");
    if (!check) {
        std::cout << "ERROR: Failed to check for ncu" << std::endl;
        return false;
    }
    
    char path[256] = {0};
    if (!fgets(path, sizeof(path), check)) {
        pclose(check);
        std::cout << "ncu not found in PATH" << std::endl;
        return false;
    }
    pclose(check);
    
    std::cout << "Found ncu at: " << path;
    
    // Try to get basic metrics using ncu
    // Note: ncu --target-processes requires the process to be running CUDA kernels
    std::string cmd = "timeout 3 ncu --target-processes " + std::to_string(pid) +
                      " --metrics sm__sass_thread_inst_executed_op_atom_pred_on.sum,"
                      "sm__thread_inst_executed.sum,launch__threads_per_block,"
                      "sm__warps_active.avg.pct_of_peak_sustained_active "
                      "--print-gpu-trace --csv 2>&1 | head -50";
    
    std::cout << "\nExecuting: " << cmd << std::endl;
    std::cout << "--- Output ---" << std::endl;
    
    FILE* ncu_output = popen(cmd.c_str(), "r");
    if (!ncu_output) {
        std::cout << "ERROR: Failed to execute ncu" << std::endl;
        return false;
    }
    
    char line[1024];
    bool found_metrics = false;
    while (fgets(line, sizeof(line), ncu_output)) {
        std::cout << line;
        if (strstr(line, "sm__sass_thread_inst_executed_op_atom") ||
            strstr(line, "launch__threads_per_block") ||
            strstr(line, "sm__warps_active")) {
            found_metrics = true;
        }
    }
    
    int exit_code = pclose(ncu_output);
    std::cout << "\nExit code: " << exit_code << std::endl;
    
    if (found_metrics) {
        std::cout << "SUCCESS: Found GPU metrics in output" << std::endl;
        return true;
    } else {
        std::cout << "WARNING: No GPU metrics found (process may not be running CUDA kernels)" << std::endl;
        return false;
    }
}

void testCUPTI() {
    std::cout << "\n=== Testing CUPTI Availability ===" << std::endl;
    
    // Check if CUPTI headers are available
    std::cout << "Checking for CUPTI headers..." << std::endl;
    
    FILE* check = popen("find /usr/local/cuda* /opt/cuda* -name 'cupti.h' 2>/dev/null | head -1", "r");
    if (!check) {
        std::cout << "ERROR: Failed to check for CUPTI" << std::endl;
        return;
    }
    
    char path[512] = {0};
    if (fgets(path, sizeof(path), check)) {
        std::cout << "Found CUPTI header at: " << path;
        std::cout << "\nCUPTI is available for programmatic access" << std::endl;
        std::cout << "Note: CUPTI requires linking against libcupti and proper initialization" << std::endl;
    } else {
        std::cout << "CUPTI headers not found" << std::endl;
    }
    pclose(check);
}

void testNvidiaSMI() {
    std::cout << "\n=== Testing nvidia-smi for process info ===" << std::endl;
    
    std::string cmd = "nvidia-smi pmon -c 1 2>/dev/null";
    std::cout << "Executing: " << cmd << std::endl;
    std::cout << "--- Output ---" << std::endl;
    
    FILE* smi_output = popen(cmd.c_str(), "r");
    if (!smi_output) {
        std::cout << "ERROR: Failed to execute nvidia-smi" << std::endl;
        return;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), smi_output)) {
        std::cout << line;
    }
    pclose(smi_output);
}

int main() {
    std::cout << "=== NVIDIA Compute APIs Test ===" << std::endl;
    std::cout << "Testing various methods to observe GPU block and thread operations\n" << std::endl;
    
    // Test 1: Get GPU processes via NVML
    std::cout << "=== Test 1: Getting GPU Processes via NVML ===" << std::endl;
    std::vector<ProcessInfo> processes = getGPUProcesses();
    
    if (processes.empty()) {
        std::cout << "No GPU processes found" << std::endl;
    } else {
        std::cout << "Found " << processes.size() << " GPU process(es):" << std::endl;
        for (const auto& p : processes) {
            std::cout << "  PID: " << p.pid 
                      << ", Memory: " << (p.memory / 1024 / 1024) << " MB"
                      << ", Name: " << p.name << std::endl;
        }
    }
    
    // Test 2: Try Nsight Compute for each process
    if (!processes.empty()) {
        std::cout << "\n=== Test 2: Testing Nsight Compute (ncu) ===" << std::endl;
        for (const auto& p : processes) {
            testNsightCompute(p.pid);
        }
    } else {
        std::cout << "\nSkipping Nsight Compute test (no processes found)" << std::endl;
    }
    
    // Test 3: Check CUPTI availability
    testCUPTI();
    
    // Test 4: Test nvidia-smi
    testNvidiaSMI();
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    std::cout << "\nSummary:" << std::endl;
    std::cout << "- NVML: Can get process PIDs and memory usage" << std::endl;
    std::cout << "- Nsight Compute (ncu): Can profile running processes (if CUDA kernels active)" << std::endl;
    std::cout << "- CUPTI: Programmatic API for detailed metrics (requires linking)" << std::endl;
    std::cout << "- nvidia-smi: System-level monitoring tool" << std::endl;
    
    return 0;
}

