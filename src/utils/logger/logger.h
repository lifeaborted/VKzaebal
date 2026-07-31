#pragma once
#include <string>

enum class LogLevel { INFO, WARNING, ERROR, DEBUG };

class Logger {
public:
    static void Init();
    static void Log(LogLevel level, const std::string& message);
    static void Close();
};