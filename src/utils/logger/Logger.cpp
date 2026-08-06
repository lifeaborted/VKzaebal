#define _CRT_SECURE_NO_WARNINGS
#include "Logger.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

static std::ofstream logFile;
std::mutex Logger::s_mutex;

void Logger::Init() {
    // Создаем папку logs, если её нет
    if (!fs::exists("logs")) {
        fs::create_directory("logs");
    }

    // Открываем файл
    logFile.open("logs/app.log", std::ios::out | std::ios::trunc);

    if (!logFile.is_open()) {
        std::cerr << "[ERROR] Failed to open log file at logs/app.log!" << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
#ifdef NDEBUG
    return;
#endif
    // Получаем текущее время
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // Форматируем время в строку [HH:MM:SS]
    std::stringstream ssTime;
    ssTime << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    std::string timeStr = "[" + ssTime.str() + "] ";

    std::string levelStr;
    switch (level) {
        case LogLevel::INFO:    levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR:   levelStr = "[ERROR] "; break;
    }

    std::string fullMessage = timeStr + levelStr + message;
    std::string clearUi = "\r\033[2K\033[1A\r\033[2K";

    std::lock_guard<std::mutex> lock(s_mutex);

    if (level == LogLevel::ERROR) {
        std::cerr << clearUi << fullMessage << "\n\n> ";
        std::cerr.flush();
    } else {
        std::cout << clearUi << fullMessage << "\n\n> ";
        std::cout.flush();
    }

    if (logFile.is_open()) {
        logFile << fullMessage << std::endl;
        logFile.flush();
    }
}

void Logger::Close() {
    if (logFile.is_open()) {
        logFile.close();
    }
}