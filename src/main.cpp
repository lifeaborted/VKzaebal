#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include <fstream>
#include <sstream>
#include <QTimer>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "utils/logger/logger.h"

std::string GetEnvVar(const std::string& filePath, const std::string& key) {
    std::ifstream file(filePath);
    if (!file.is_open()) return "";
    std::string line, searchKey = key + "=";
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

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Logger::Init();
    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");

    AudioEngine audio;
    if (!audio.Init()) {
        return -1;
    }

    PlaylistManager playlist;
    VkApiClient vkClient;

    // Связываем Аудиодвижок и Плейлист (когда трек кончился -> включаем следующий)
    audio.OnTrackFinished = [&]() {
        playlist.Next();
    };

    // Связываем Плейлист и Аудиодвижок (запрос трека -> запрос URL -> воспроизведение)
    // Рекурсивная лямбда для повторных попыток загрузки и воспроизведения
    std::function<void(const Track&, int)> attemptPlay;
    attemptPlay = [&](const Track& track, int attempt) {
        Logger::Log(LogLevel::INFO, ">>> Requesting fresh URL for: " + track.artist + " - " + track.title + " (Attempt " + std::to_string(attempt) + "/3)");

        vkClient.FetchTrackUrl(track.id, [&audio, &playlist, track, attempt, &attemptPlay](const std::string& freshUrl) {
            bool success = false;

            if (!freshUrl.empty()) {
                success = audio.PlayStream(freshUrl);
            }

            if (success) {
                Logger::Log(LogLevel::INFO, ">>> Playing: " + track.artist + " - " + track.title + " [" + track.GetFormattedDuration() + "]");
            } else {
                Logger::Log(LogLevel::WARNING, "Playback failed for track: " + track.title);

                if (attempt < 3) {
                    Logger::Log(LogLevel::INFO, "Retrying in 2 seconds...");
                    // Ждем 2 секунды, чтобы дать сети "отдышаться", и пробуем снова
                    QTimer::singleShot(2000, [track, attempt, &attemptPlay]() {
                        attemptPlay(track, attempt + 1);
                    });
                } else {
                    Logger::Log(LogLevel::ERROR, "Max retries reached. Skipping track.");
                    playlist.Next();
                }
            }
        });
    };

    // Привязываем наш умный обработчик к плейлисту
    playlist.OnTrackRequested = [&](const Track& track) {
        attemptPlay(track, 1);
    };

// Интеграция VK
    std::string myVkToken = GetEnvVar(".env", "VK_TOKEN");
    if (!myVkToken.empty()) vkClient.SetAccessToken(myVkToken);

    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
            // Проверяем, пуст ли плейлист до добавления новых треков
            bool isFirstChunk = !playlist.HasTracks();

            for (const auto& track : tracks) {
                playlist.AddTrack(track);
            }

            std::cout << "[Плейлист] Загружена порция из " << tracks.size() << " треков." << std::endl;

            if (isFirstChunk && playlist.HasTracks()) {
                std::cout << "\n=== ПЕРВАЯ ЧАСТЬ ПЛЕЙЛИСТА ЗАГРУЖЕНА, НАЧИНАЕМ ВОСПРОИЗВЕДЕНИЕ ===\n" << std::endl;
                playlist.OnTrackRequested(playlist.GetCurrentTrack());
            }
        });

    vkClient.FetchAllUserAudio();

    // Консольный ввод
    std::cout << "\nControls:\n [P] Play/Pause\n [N] Next\n [B] Prev\n [+] Vol Up\n [-] Vol Down\n [S] Shuffle\n [R] Repeat Mode\n [J <num>] Jump to track (e.g., J 5)\n [Q] Quit\n-----------------------\n";

    std::thread inputThread([&]() {
        char command;
        bool isRunning = true;
        while (isRunning) {
            std::cin >> command;
            switch (std::tolower(command)) {
                case 'p':
                    QMetaObject::invokeMethod(&app, [&]() {
                        if (audio.IsPlaying()) audio.Pause();
                        else audio.Resume();
                    }, Qt::QueuedConnection);
                    break;
                case 'n':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection);
                    break;
                case 'b':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection);
                    break;
                case '+':
                    QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() + 0.1f); }, Qt::QueuedConnection);
                    break;
                case '-':
                    QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() - 0.1f); }, Qt::QueuedConnection);
                    break;
                case 's':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleShuffle(); }, Qt::QueuedConnection);
                    break;
                case 'r':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleRepeat(); }, Qt::QueuedConnection);
                    break;
                case 'j': {
                    int index;
                    if (std::cin >> index) {
                        QMetaObject::invokeMethod(&app, [&playlist, index]() { playlist.JumpTo(index - 1); }, Qt::QueuedConnection);
                    }
                    break;
                }
                case 'q':
                    isRunning = false;
                    QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                    break;
            }
        }
    });

    int exitCode = app.exec();
    if (inputThread.joinable()) inputThread.join();

    Logger::Close();
    return exitCode;
}