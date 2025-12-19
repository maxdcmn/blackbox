#include "services/vram_tracker.h"
#include "services/nvml_utils.h"
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <absl/strings/str_cat.h>

std::map<std::string, ProcessVRAM> getProcessVRAMUsage() {
    std::map<std::string, ProcessVRAM> usage;
    DetailedVRAMInfo info = getDetailedVRAMUsage();
    
    for (const auto& proc : info.processes) {
        ProcessVRAM pvram;
        pvram.pid = proc.pid;
        pvram.used_bytes = proc.used_bytes;
        pvram.total_bytes = info.total;
        pvram.usage_percent = info.total > 0 ? (100.0 * proc.used_bytes / info.total) : 0.0;
        
        std::string key = absl::StrCat("pid_", proc.pid);
        usage[key] = pvram;
        
        if (proc.name.find("python") != std::string::npos || 
            proc.name.find("vllm") != std::string::npos) {
            usage[proc.name] = pvram;
        }
    }
    
    return usage;
}

double getModelVRAMUsagePercent(const std::string& container_name, unsigned int pid) {
    auto usage = getProcessVRAMUsage();
    
    std::string pid_key = absl::StrCat("pid_", pid);
    if (usage.find(pid_key) != usage.end()) {
        return usage[pid_key].usage_percent;
    }
    
    for (const auto& [key, pvram] : usage) {
        if (pvram.pid == pid) {
            return pvram.usage_percent;
        }
    }
    
    return 0.0;
}

