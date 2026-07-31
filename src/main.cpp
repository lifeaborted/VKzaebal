#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include "core/audio/AudioEngine/AudioEngine.h"
#include "utils/logger/logger.h"
#include "core/network/NetworkStreamer.h"

int main(int argc, char *argv[]) {
    // Инициализируем запись в файл
    Logger::Init();

    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Engine Console Started ---");

    AudioEngine engine;

    if (!engine.InitializeStream()) {
        Logger::Close();
        return -1;
    }
    //engine.Play();

    NetworkStreamer streamer;

    // НАСТРОЙКА ВАТЕРМАРОК
    engine.OnBufferFull = [&streamer]() {
        // Можно вызывать напрямую, но invokeMethod гарантирует потокобезопасность
        QMetaObject::invokeMethod(&streamer, "PauseDownload", Qt::QueuedConnection);
    };

    engine.OnBufferNeedsData = [&streamer]() {
        // Перебрасываем вызов в главный поток.
        QMetaObject::invokeMethod(&streamer, "ResumeDownload", Qt::QueuedConnection);
    };

    // Сеть сообщает, что файл скачан целиком
    QObject::connect(&streamer, &NetworkStreamer::DownloadFinished, [&]() {
        engine.MarkStreamFinished();
    });

    // Движок сообщает, что последние байты доиграли в колонках
    engine.OnTrackFinished = [&]() {
        // ВАЖНО: Коллбэк вызывается из аудиопотока miniaudio!
        // Перебрасываем исполнение в главный поток Qt для безопасности.
        QMetaObject::invokeMethod(&app, [&]() {
            Logger::Log(LogLevel::INFO, "--- TRACK COMPLETION EVENT RECEIVED ---");
            // В будущем здесь будет вызов streamer.StartDownload(nextUrl);
        }, Qt::QueuedConnection);
    };

    // Связываем сигнал от сети с аудиодвижком
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(data.constData());
        engine.PushAudioData(rawData, data.size());
    });

    std::string testUrl = "https://samplelib.com/mp3/sample-10s.mp3";
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