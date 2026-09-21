#define _CRT_SECURE_NO_WARNINGS
#include "Logger.h" // Обязательно подключаем заголовок
#include "utils/path/PathManager.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static std::ofstream logFile;
std::mutex Logger::s_mutex;

// Инициализация минимального уровня логов
std::atomic<LogLevel> Logger::s_minLogLevel = LogLevel::INFO;
std::atomic<bool> Logger::s_consoleOutputEnabled = true;
Logger::LogCallback Logger::s_callback = nullptr;

void Logger::SetLogCallback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_callback = std::move(callback);
}

void Logger::Init() {
    PathManager::Init();
    std::string logPath = PathManager::GetLogFilePath().toStdString();

    // Открываем файл
    logFile.open(logPath, std::ios::out | std::ios::trunc);

    if (!logFile.is_open()) {
        std::cerr << "[ERROR] Failed to open log file at " << logPath << std::endl;
    }
}

void Logger::SetMinLogLevel(LogLevel level) {
    s_minLogLevel.store(level, std::memory_order_relaxed);
}

void Logger::Log(LogLevel level, const std::string& message) {
    // --- 1. Ограничение вывода через макросы (Compile-time) ---
    // Если собираем в Release (NDEBUG) и хотим видеть только ошибки:
    //#ifdef NDEBUG
    //    if (level != LogLevel::ERROR) return;
    //#endif

    // --- 2. Ограничение вывода через переменную (Runtime) ---
    // Так как enum: DEBUG=0, INFO=1, WARNING=2, ERROR=3
    // Если текущий уровень меньше установленного порога, просто выходим
    if (level < s_minLogLevel.load(std::memory_order_relaxed)) {
        return;
    }

    // Получаем текущее время
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Потокобезопасное форматирование времени в строку [HH:MM:SS]
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &in_time_t);
#else
    localtime_r(&in_time_t, &timeInfo);
#endif

    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "[%H:%M:%S] ", &timeInfo);
    std::string timeStr = timeBuf;

    std::string levelStr;
    switch (level) {
        case LogLevel::DEBUG:   levelStr = "[DEBUG] "; break;
        case LogLevel::INFO:    levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR:   levelStr = "[ERROR] "; break;
    }

    std::string fullMessage = timeStr + levelStr + message;
    std::string clearUi = "\r\033[2K\033[1A\r\033[2K";

    LogCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);

        if (s_consoleOutputEnabled.load(std::memory_order_relaxed)) {
            if (level == LogLevel::ERROR) {
                std::cerr << clearUi << fullMessage << "\n\n> ";
                std::cerr.flush();
            } else {
                std::cout << clearUi << fullMessage << "\n\n> ";
                std::cout.flush();
            }
        }

        // В файл логируем всё, что прошло фильтр
        if (logFile.is_open()) {
            logFile << fullMessage << std::endl;
            logFile.flush();
        }

        cb = s_callback;
    }

    // Вызываем callback ВНЕ мьютекса, чтобы избежать взаимоблокировки (deadlock)
    if (cb) {
        cb(level, message);
    }
}

void Logger::SetConsoleOutputEnabled(bool enabled) {
    s_consoleOutputEnabled.store(enabled, std::memory_order_relaxed);
}

void Logger::Close() {
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_callback = nullptr;
    }
    if (logFile.is_open()) {
        logFile.close();
    }
}