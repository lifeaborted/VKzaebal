#pragma once
#include <string>
#include <mutex>

#ifdef _WIN32
#undef ERROR
#endif

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

class Logger {
public:
    static void Init();
    static void Log(LogLevel level, const std::string& message);
    static void Close();
    static std::mutex& GetMutex() { return s_mutex; }

    static void SetMinLogLevel(LogLevel level);
    static void SetConsoleOutputEnabled(bool enabled);
private:
    static std::mutex s_mutex;
    static LogLevel s_minLogLevel;
    static bool s_consoleOutputEnabled;
};