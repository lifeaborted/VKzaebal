#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include <fstream>
#include <sstream>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/network/NetworkStreamer.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "utils/logger/logger.h"

// функция чтения значений из .env файла
// функция чтения значений из .env файла
std::string GetEnvVar(const std::string& filePath, const std::string& key) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Logger::Log(LogLevel::WARNING, "Could not open " + filePath + " file.");
        return "";
    }

    std::string line;
    std::string searchKey = key + "=";
    while (std::getline(file, line)) {
        if (line.find(searchKey) == 0) {
            std::string value = line.substr(searchKey.length());
            // Обрезаем пробелы с начала
            value.erase(0, value.find_first_not_of(" \n\r\t"));
            // Обрезаем \r, \n и пробелы с конца
            value.erase(value.find_last_not_of(" \n\r\t") + 1);
            return value;
        }
    }
    return "";
}

int main(int argc, char *argv[]) {
    // Инициализируем запись логов
    Logger::Init();

    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Engine Console Started ---");

    AudioEngine engine;
    NetworkStreamer streamer;
    PlaylistManager playlist;
    VkApiClient vkClient;

    // ==========================================
    // 1. НАСТРОЙКА СВЯЗИ СЕТИ И АУДИОДВИЖКА
    // ==========================================

    // Заталкиваем скачанные байты в буфер
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(data.constData());
        engine.PushAudioData(rawData, data.size());
    });

    // Управление потоком скачивания (Backpressure)
    engine.OnBufferFull = [&streamer]() {
        QMetaObject::invokeMethod(&streamer, "PauseDownload", Qt::QueuedConnection);
    };

    engine.OnBufferNeedsData = [&streamer]() {
        QMetaObject::invokeMethod(&streamer, "ResumeDownload", Qt::QueuedConnection);
    };

    // Оповещаем движок, что файл докачался
    QObject::connect(&streamer, &NetworkStreamer::DownloadFinished, [&]() {
        engine.MarkStreamFinished();
    });

    // ==========================================
    // 2. НАСТРОЙКА МЕНЕДЖЕРА ПЛЕЙЛИСТА
    // ==========================================

    // Реакция на запрос нового трека (переключение)
    playlist.OnTrackRequested = [&](const std::string& url) {
        Logger::Log(LogLevel::INFO, ">>> Changing track to: " + url + " <<<");

        streamer.StopDownload();
        engine.Shutdown();

        if (!engine.InitializeStream()) {
            Logger::Log(LogLevel::ERROR, "Failed to initialize stream for new track.");
            return;
        }

        streamer.StartDownload(url);
    };

    // Реакция на физический конец текущего трека (EOF)
    engine.OnTrackFinished = [&]() {
        QMetaObject::invokeMethod(&app, [&]() {
            Logger::Log(LogLevel::INFO, "--- TRACK COMPLETION EVENT RECEIVED ---");
            playlist.Next(); // Автоматически включаем следующий
        }, Qt::QueuedConnection);
    };


    // ==========================================
    // 3. ИНТЕГРАЦИЯ VK API
    // ==========================================

    std::string myVkToken = GetEnvVar(".env", "VK_TOKEN");
    if (!myVkToken.empty()) {
        vkClient.SetAccessToken(myVkToken);
        Logger::Log(LogLevel::INFO, "VK Token successfully loaded from .env");
    } else {
        Logger::Log(LogLevel::ERROR, "VK Token is missing or .env file not found! Playback won't start.");
    }

    vkClient.SetAccessToken(myVkToken);

    // Подключаемся к сигналу успешной загрузки аудио
    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        std::cout << "\n=== СКАЧАН ПЛЕЙЛИСТ VK ===" << std::endl;

        for (const auto& track : tracks) {
            // Красиво выводим каждый трек с нашим форматированием времени
            std::cout << "- " << track.artist << " - " << track.title
                      << " [" << track.GetFormattedDuration() << "]" << std::endl;

            // Добавляем прямую ссылку в менеджер очереди
            playlist.AddTrack(track.url);
        }
        std::cout << "==========================\n" << std::endl;

        // Как только плейлист загружен, сразу включаем первый трек
        if (playlist.HasTracks()) {
            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    // Если токен указан, запрашиваем список (допустим, первые 50 треков твоего профиля)
    if (myVkToken != "YOUR_KATE_MOBILE_ACCESS_TOKEN_HERE") {
        Logger::Log(LogLevel::INFO, "Requesting audio from VK...");
        vkClient.FetchUserAudio(0, 50); // ownerId = 0 означает "текущий авторизованный пользователь"
    } else {
        Logger::Log(LogLevel::WARNING, "VK Token is missing! Playback won't start.");
    }

    // ==========================================
    // 4. КОНСОЛЬНОЕ УПРАВЛЕНИЕ
    // ==========================================

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
                case 'n':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection);
                    break;
                case 'b':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection);
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

    Logger::Close();
    return exitCode;
}