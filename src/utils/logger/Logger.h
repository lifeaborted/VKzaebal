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
private:
    static std::mutex s_mutex;
};