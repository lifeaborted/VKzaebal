#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include "core/audio/AudioEngine/AudioEngine.h"
#include "utils/logger/logger.h"
#include "core/audio/network/NetworkStreamer.h"

int main(int argc, char *argv[]) {
    // Инициализируем запись в файл
    Logger::Init();

    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Engine Console Started ---");

    AudioEngine engine;

    std::string filePath = "C:\\others\\Codes\\audio player\\src\\test.mp3";
    if (!engine.Initialize(filePath)) {
        Logger::Close(); // Не забываем закрыть при ошибке
        return -1;
    }
    engine.Play();

    NetworkStreamer streamer;
    std::string testUrl = "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3";
    streamer.StartDownload(testUrl);

    std::cout << "\nControls:\n";
    std::cout << " [P] Play/Pause\n";
    std::cout << " [+] Volume Up\n";
    std::cout << " [-] Volume Down\n";
    std::cout << " [Q] Quit\n";
    std::cout << "-----------------------\n";

    float currentVolume = 1.0f;
    bool isPaused = false;

    std::thread inputThread([&]() {
        char command;
        bool isRunning = true;
        while (isRunning) {
            std::cin >> command;
            command = std::tolower(command);

            switch (command) {
                case 'p':
                    isPaused ? engine.Play() : engine.Pause();
                    isPaused = !isPaused;
                    break;
                case '+':
                    currentVolume += 0.1f;
                    engine.SetVolume(currentVolume);
                    break;
                case '-':
                    currentVolume -= 0.1f;
                    engine.SetVolume(currentVolume);
                    break;
                case 'q':
                    Logger::Log(LogLevel::INFO, "Quit command received.");
                    isRunning = false;
                    QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                    break;
                default:
                    Logger::Log(LogLevel::WARNING, "Unknown command.");
                    break;
            }
        }
    });

    int exitCode = app.exec();

    Logger::Log(LogLevel::INFO, "Press any key + Enter to completely close the terminal...");
    if (inputThread.joinable()) {
        inputThread.join();
    }

    // Закрываем файл перед выходом
    Logger::Close();
    return exitCode;
}