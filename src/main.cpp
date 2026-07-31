#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include <fstream>
#include <sstream>

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>

#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "utils/logger/logger.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef ERROR
#endif

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
            value.erase(0, value.find_first_not_of(" \n\r\t"));
            value.erase(value.find_last_not_of(" \n\r\t") + 1);
            return value;
        }
    }
    return "";
}

// Функция-фильтр для подавления спама от Qt Multimedia
void SuppressQtLogs(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // Пропускаем только фатальные краши самого Qt
    if (type == QtFatalMsg) {
        fprintf(stderr, "Qt Fatal: %s\n", msg.toLocal8Bit().constData());
    }
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Logger::Init();
    qputenv("QT_MEDIA_BACKEND_FFMPEG_LOGLEVEL", "error");
    qputenv("QT_DEBUG_PLUGINS", "0");
    qputenv("QT_MEDIA_BACKEND_FFMPEG_LOGLEVEL", "error");
    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Engine (QtMultimedia) Started ---");

    // Инициализация плеера Qt
    QMediaPlayer* player = new QMediaPlayer(&app);
    QAudioOutput* audioOutput = new QAudioOutput(&app);
    player->setAudioOutput(audioOutput);
    audioOutput->setVolume(1.0f); // 1.0 = 100%

    PlaylistManager playlist;
    VkApiClient vkClient;

    // ==========================================
    // 1. НАСТРОЙКА МЕНЕДЖЕРА ПЛЕЙЛИСТА
    // ==========================================

    playlist.OnTrackRequested = [&](const std::string& url) {
        Logger::Log(LogLevel::INFO, ">>> Changing track to: " + url + " <<<");

        // Все вызовы QMediaPlayer должны происходить в главном потоке Qt
        QMetaObject::invokeMethod(&app, [player, url]() {
            player->setSource(QUrl(QString::fromStdString(url)));
            player->play();
        }, Qt::QueuedConnection);
    };

    // Слушаем сигнал Qt о завершении трека
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            Logger::Log(LogLevel::INFO, "--- TRACK COMPLETION EVENT RECEIVED ---");
            playlist.Next();
        } else if (status == QMediaPlayer::InvalidMedia) {
            Logger::Log(LogLevel::ERROR, "Media error: " + player->errorString().toStdString());
        }
    });

    // ==========================================
    // 2. ИНТЕГРАЦИЯ VK API
    // ==========================================

    std::string myVkToken = GetEnvVar(".env", "VK_TOKEN");
    if (!myVkToken.empty()) {
        vkClient.SetAccessToken(myVkToken);
        Logger::Log(LogLevel::INFO, "VK Token successfully loaded from .env");
    } else {
        Logger::Log(LogLevel::ERROR, "VK Token is missing!");
    }

    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        std::cout << "\n=== ПЛЕЙЛИСТ VK ЗАГРУЖЕН ===" << std::endl;
        for (const auto& track : tracks) {
            std::cout << "- " << track.artist << " - " << track.title
                      << " [" << track.GetFormattedDuration() << "]" << std::endl;
            playlist.AddTrack(track.url);
        }
        std::cout << "==========================\n" << std::endl;

        if (playlist.HasTracks()) {
            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    Logger::Log(LogLevel::INFO, "Requesting audio from VK...");
    vkClient.FetchUserAudio(0, 50);

    // ==========================================
    // 3. КОНСОЛЬНОЕ УПРАВЛЕНИЕ
    // ==========================================

    std::cout << "\nControls:\n [P] Play/Pause\n [N] Next Track\n [B] Previous Track\n [+] Volume Up\n [-] Volume Down\n [Q] Quit\n-----------------------\n";

    std::thread inputThread([&]() {
        char command;
        bool isRunning = true;
        while (isRunning) {
            std::cin >> command;
            command = std::tolower(command);

            switch (command) {
                case 'p':
                    QMetaObject::invokeMethod(&app, [player]() {
                        if (player->playbackState() == QMediaPlayer::PlayingState) {
                            player->pause();
                        } else {
                            player->play();
                        }
                    }, Qt::QueuedConnection);
                    break;
                case 'n':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection);
                    break;
                case 'b':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection);
                    break;
                case '+':
                    QMetaObject::invokeMethod(&app, [audioOutput]() {
                        float vol = std::min(1.0f, audioOutput->volume() + 0.1f);
                        audioOutput->setVolume(vol);
                    }, Qt::QueuedConnection);
                    break;
                case '-':
                    QMetaObject::invokeMethod(&app, [audioOutput]() {
                        float vol = std::max(0.0f, audioOutput->volume() - 0.1f);
                        audioOutput->setVolume(vol);
                    }, Qt::QueuedConnection);
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