#include "services/nsight_utils.h"
#include <cstdio>
#include <string>
#include <absl/strings/str_cat.h>

NsightMetrics getNsightMetrics(unsigned int pid) {
    NsightMetrics metrics{};
    metrics.atomic_operations = 0;
    metrics.threads_per_block = 0;
    metrics.occupancy = 0.0;
    metrics.active_blocks = 0;
    metrics.memory_throughput = 0;
    metrics.dram_read_bytes = 0;
    metrics.dram_write_bytes = 0;
    metrics.available = false;
    
    FILE* ncu_check = popen("which ncu > /dev/null 2>&1", "r");
    if (!ncu_check) {
        return metrics;
    }
    int ncu_available = pclose(ncu_check);
    if (ncu_available != 0) {
        return metrics;
    }
    
    std::string cmd = absl::StrCat(
        "timeout 2 ncu --target-processes ", pid,
        " --metrics sm__sass_thread_inst_executed_op_atom_pred_on.sum,sm__thread_inst_executed.sum,sm__warps_active.avg.pct_of_peak_sustained_active,dram__bytes_read.sum,dram__bytes_write.sum",
        " --print-gpu-trace --csv 2>/dev/null | tail -30"
    );
    
    FILE* ncu_output = popen(cmd.c_str(), "r");
    if (!ncu_output) {
        return metrics;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), ncu_output)) {
        std::string line_str(line);
        if (line_str.find("sm__sass_thread_inst_executed_op_atom") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.atomic_operations = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        if (line_str.find("launch__threads_per_block") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.threads_per_block = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        if (line_str.find("sm__warps_active") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.occupancy = std::stod(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        if (line_str.find("dram__bytes_read") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.dram_read_bytes = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
        if (line_str.find("dram__bytes_write") != std::string::npos) {
            size_t last_space = line_str.find_last_of(" \t,");
            if (last_space != std::string::npos) {
                try {
                    metrics.dram_write_bytes = std::stoull(line_str.substr(last_space + 1));
                } catch (...) {}
            }
        }
    }
    pclose(ncu_output);
    
    if (metrics.atomic_operations > 0 || metrics.threads_per_block > 0) {
        metrics.available = true;
    }
    
    return metrics;
}

