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

    // Добавляем метод для изменения минимального уровня вывода
    static void SetMinLogLevel(LogLevel level);
private:
    static std::mutex s_mutex;
    static LogLevel s_minLogLevel; // Переменная для хранения текущего порога
};