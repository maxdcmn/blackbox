#pragma once

#include <string>
#include <map>

std::map<std::string, std::string> loadEnvFile(const std::string& path = ".env");
std::string getEnvValue(const std::string& key, const std::string& default_val = "");
bool hasEnvKey(const std::string& key);

