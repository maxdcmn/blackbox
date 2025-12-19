#pragma once

#include <string>

std::string parseJSONField(const std::string& json, const std::string& field);
int parseJSONInt(const std::string& json, const std::string& field, int default_val = 0);






