#define _CRT_SECURE_NO_WARNINGS // Отключаем паранойю MSVC на старые функции времени
#include "logger.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

// Глобальная переменная, скрытая внутри этого файла
static std::ofstream logFile;

void Logger::Init() {
    // Создаем папку logs, если её нет
    if (!fs::exists("logs")) {
        fs::create_directory("logs");
    }
    
    // Открываем файл. ios::trunc означает, что старое содержимое будет удалено
    logFile.open("logs/app.log", std::ios::out | std::ios::trunc);
    
    if (!logFile.is_open()) {
        std::cerr << "[ERROR] Failed to open log file at logs/app.log!" << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    // Получаем текущее время
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    // Форматируем время в строку [HH:MM:SS]
    std::stringstream ssTime;
    ssTime << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    std::string timeStr = "[" + ssTime.str() + "] ";

    // Формируем тег уровня логирования
    std::string levelStr;
    switch (level) {
        case LogLevel::INFO:    levelStr = "[INFO] "; break;
        case LogLevel::WARNING: levelStr = "[WARN] "; break;
        case LogLevel::ERROR:   levelStr = "[ERROR] "; break;
    }

    // Собираем итоговое сообщение
    std::string fullMessage = timeStr + levelStr + message;

    // Выводим в консоль
    if (level == LogLevel::ERROR) {
        std::cerr << fullMessage << std::endl;
    } else {
        std::cout << fullMessage << std::endl;
    }

    // Записываем в файл и сразу сбрасываем буфер (flush),
    // чтобы при краше программы логи не потерялись
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