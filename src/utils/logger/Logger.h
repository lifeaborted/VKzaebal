#pragma once
#include <string>
#include <mutex>
#include <functional>
#include <atomic>

#ifdef _WIN32
#undef ERROR
#endif

enum class LogLevel { DEBUG, INFO, WARNING, ERROR };

class Logger {
public:
    using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

    static void Init();
    static void Log(LogLevel level, const std::string& message);
    static void Close();
    static std::mutex& GetMutex() { return s_mutex; }

    static void SetMinLogLevel(LogLevel level);
    static void SetConsoleOutputEnabled(bool enabled);
    static void SetLogCallback(LogCallback callback);
private:
    static std::mutex s_mutex;
    static std::atomic<LogLevel> s_minLogLevel;
    static std::atomic<bool> s_consoleOutputEnabled;
    static LogCallback s_callback;
};