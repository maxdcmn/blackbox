#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void log(LogLevel level, const std::string& message);
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
    
    static void setLevel(LogLevel level);
    static LogLevel getLevel();
    
private:
    static LogLevel current_level;
    static std::string getTimestamp();
    static std::string levelToString(LogLevel level);
    static std::string colorize(LogLevel level, const std::string& text);
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::debug(msg)
#define LOG_INFO(msg) Logger::info(msg)
#define LOG_WARN(msg) Logger::warn(msg)
#define LOG_ERROR(msg) Logger::error(msg)


