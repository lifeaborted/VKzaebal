#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "utils/logger/logger.h"
#include "core/network/NetworkStreamer.h"
#include "core/playlist/PlaylistManager.h"

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

    // --- НАСТРОЙКА ПЛЕЙЛИСТА ---
    PlaylistManager playlist;
    playlist.AddTrack("https://samplelib.com/mp3/sample-10s.mp3");
    playlist.AddTrack("https://samplelib.com/mp3/sample-12s.mp3"); // Для проверки переключения

    // Главный обработчик смены трека
    playlist.OnTrackRequested = [&](const std::string& url) {
        Logger::Log(LogLevel::INFO, ">>> Changing track to: " + url + " <<<");

        // 1. Глушим текущие процессы
        streamer.StopDownload();
        engine.Shutdown();

        // 2. Инициализируем чистую память под новый трек
        if (!engine.InitializeStream()) {
            Logger::Log(LogLevel::ERROR, "Failed to initialize stream for new track.");
            return;
        }

        // 3. Запускаем скачивание
        streamer.StartDownload(url);
    };

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

    // --- ОБРАБОТКА КОНЦА ТРЕКА ---
    engine.OnTrackFinished = [&]() {
        QMetaObject::invokeMethod(&app, [&]() {
            Logger::Log(LogLevel::INFO, "--- TRACK COMPLETION EVENT RECEIVED ---");
            // Автоматически включаем следующий трек
            playlist.Next();
        }, Qt::QueuedConnection);
    };

    // Связываем сигнал от сети с аудиодвижком
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(data.constData());
        engine.PushAudioData(rawData, data.size());
    });

    // Запускаем первый трек в очереди
    if (playlist.HasTracks()) {
        playlist.OnTrackRequested(playlist.GetCurrentTrack());
    }

    std::cout << "\nControls:\n";
    std::cout << " [P] Play/Pause\n";
    std::cout << " [N] Next Track\n";
    std::cout << " [B] Previous Track\n";
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
                case 'n':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection);
                    break;
                case 'b':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection);
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