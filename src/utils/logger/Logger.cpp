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
LogLevel Logger::s_minLogLevel = LogLevel::ERROR;
bool Logger::s_consoleOutputEnabled = true;

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
    s_minLogLevel = level;
}

void Logger::Log(LogLevel level, const std::string& message) {
    // --- 1. Ограничение вывода через макросы (Compile-time) ---
    // Если собираем в Release (NDEBUG) и хотим видеть только ошибки:
    #ifdef NDEBUG
        if (level != LogLevel::ERROR) return;
    #endif

    // --- 2. Ограничение вывода через переменную (Runtime) ---
    // Так как enum: DEBUG=0, INFO=1, WARNING=2, ERROR=3
    // Если текущий уровень меньше установленного порога, просто выходим
    if (level < s_minLogLevel) {
        return;
    }

    // Получаем текущее время
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Форматируем время в строку [HH:MM:SS]
    std::stringstream ssTime;
    ssTime << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    std::string timeStr = "[" + ssTime.str() + "] ";

    std::string levelStr;
    switch (level) {
        case LogLevel::DEBUG:   levelStr = "[DEBUG] "; break;
        case LogLevel::INFO:    levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR:   levelStr = "[ERROR] "; break;
    }

    std::string fullMessage = timeStr + levelStr + message;
    std::string clearUi = "\r\033[2K\033[1A\r\033[2K";

    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_consoleOutputEnabled) {
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
}

void Logger::SetConsoleOutputEnabled(bool enabled) {
    s_consoleOutputEnabled = enabled;
}

void Logger::Close() {
    if (logFile.is_open()) {
        logFile.close();
    }
}