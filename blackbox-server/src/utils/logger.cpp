#include "utils/logger.h"
#include "utils/env_utils.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <cctype>
#include <algorithm>

// Initialize log level from environment variable or default to INFO
LogLevel Logger::current_level = []() {
    const char* log_level_env = std::getenv("LOG_LEVEL");
    if (log_level_env != nullptr) {
        std::string level_str = log_level_env;
        // Convert to uppercase for case-insensitive comparison
        std::transform(level_str.begin(), level_str.end(), level_str.begin(), ::toupper);
        if (level_str == "DEBUG") return LogLevel::DEBUG;
        if (level_str == "INFO") return LogLevel::INFO;
        if (level_str == "WARN") return LogLevel::WARN;
        if (level_str == "ERROR") return LogLevel::ERROR;
    }
    return LogLevel::INFO;
}();

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::colorize(LogLevel level, const std::string& text) {
    // Check if we should use colors (check if output is a TTY)
    static bool use_colors = []() {
        const char* term = std::getenv("TERM");
        return term != nullptr && std::string(term) != "dumb";
    }();
    
    if (!use_colors) {
        return text;
    }
    
    switch (level) {
        case LogLevel::DEBUG: return "\033[36m" + text + "\033[0m"; // Cyan
        case LogLevel::INFO:  return "\033[32m" + text + "\033[0m"; // Green
        case LogLevel::WARN:  return "\033[33m" + text + "\033[0m"; // Yellow
        case LogLevel::ERROR: return "\033[31m" + text + "\033[0m"; // Red
        default: return text;
    }
}

void Logger::setLevel(LogLevel level) {
    current_level = level;
}

LogLevel Logger::getLevel() {
    return current_level;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < current_level) {
        return;
    }
    
    std::string timestamp = getTimestamp();
    std::string level_str = levelToString(level);
    std::string colored_level = colorize(level, level_str);
    
    std::cerr << "[" << timestamp << "] [" << colored_level << "] " << message << std::endl;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}


