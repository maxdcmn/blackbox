#pragma once

#include <string>
#include <map>
#include <deque>

struct ProcessVRAM {
    unsigned int pid;
    unsigned long long used_bytes;
    unsigned long long total_bytes;
    double usage_percent;
};

std::map<std::string, ProcessVRAM> getProcessVRAMUsage();
double getModelVRAMUsagePercent(const std::string& container_name, unsigned int pid);






