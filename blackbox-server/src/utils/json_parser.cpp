#include "utils/json_parser.h"
#include <nlohmann/json.hpp>
#include <string>

std::string parseJSONField(const std::string& json, const std::string& field) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        if (j.contains(field) && j[field].is_string()) {
            return j[field].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        // Invalid JSON, return empty string
    }
    return "";
}

int parseJSONInt(const std::string& json, const std::string& field, int default_val) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        if (j.contains(field)) {
            if (j[field].is_number()) {
                return j[field].get<int>();
            } else if (j[field].is_string()) {
                return std::stoi(j[field].get<std::string>());
            }
        }
    } catch (const nlohmann::json::exception& e) {
        // Invalid JSON, return default
    } catch (const std::exception& e) {
        // Conversion error, return default
    }
    return default_val;
}


