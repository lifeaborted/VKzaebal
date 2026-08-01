#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

#include <QApplication> // ИЗМЕНЕНО: Был QCoreApplication
#include <QMetaObject>
#include <QTimer>
#include <QtWebView/QtWebView>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/logger/logger.h"

const int VK_APP_ID = 2685278; // ID приложения KateMobile

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Инициализация WebView ДО QApplication
    QtWebView::initialize();
    QApplication app(argc, argv); // ИЗМЕНЕНО

    Logger::Init();
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");

    AudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    VkApiClient vkClient;
    VkAuthManager authManager;

    // --- НАСТРОЙКА АУДИОДВИЖКА И ПЛЕЙЛИСТА ---
    audio.OnTrackFinished = [&]() { playlist.Next(); };

    std::function<void(const Track&, int)> attemptPlay;
    attemptPlay = [&](const Track& track, int attempt) {
        Logger::Log(LogLevel::INFO, ">>> Requesting fresh URL for: " + track.artist + " - " + track.title + " (Attempt " + std::to_string(attempt) + "/3)");

        vkClient.FetchTrackUrl(track.id, [&audio, &playlist, track, attempt, &attemptPlay](const std::string& freshUrl) {
            if (!freshUrl.empty() && audio.PlayStream(freshUrl)) {
                Logger::Log(LogLevel::INFO, ">>> Playing: " + track.artist + " - " + track.title + " [" + track.GetFormattedDuration() + "]");
            } else {
                Logger::Log(LogLevel::WARNING, "Playback failed for track: " + track.title);
                if (attempt < 3) {
                    Logger::Log(LogLevel::INFO, "Retrying in 2 seconds...");
                    QTimer::singleShot(2000, [track, attempt, &attemptPlay]() { attemptPlay(track, attempt + 1); });
                } else {
                    Logger::Log(LogLevel::ERROR, "Max retries reached. Skipping track.");
                    playlist.Next();
                }
            }
        });
    };

    playlist.OnTrackRequested = [&](const Track& track) { attemptPlay(track, 1); };

    // --- ЛОГИКА АВТОРИЗАЦИИ И СКАЧИВАНИЯ ---
    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        bool isFirstChunk = !playlist.HasTracks();
        for (const auto& track : tracks) playlist.AddTrack(track);
        std::cout << "[Плейлист] Загружена порция из " << tracks.size() << " треков.\n";

        if (isFirstChunk && playlist.HasTracks()) {
            std::cout << "\n=== ПЕРВАЯ ЧАСТЬ ЗАГРУЖЕНА. НАЧИНАЕМ ВОСПРОИЗВЕДЕНИЕ ===\n\n"
                      << "Controls:\n [P] Play/Pause\n [N] Next\n [B] Prev\n"
                      << " [+] Vol Up\n [-] Vol Down\n [S] Shuffle\n [R] Repeat Mode\n"
                      << " [J <num>] Jump to track\n [Q] Quit\n-----------------------\n> ";
            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    QObject::connect(&authManager, &VkAuthManager::TokenReceived, [&](const std::string& token) {
        authManager.SaveToken(token);
        vkClient.SetAccessToken(token);
        std::cout << "\n[УСПЕХ] Токен перехвачен! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    });

    QObject::connect(&authManager, &VkAuthManager::AuthFailed, [&](const std::string& error) {
        std::cout << "\n[ОШИБКА АВТОРИЗАЦИИ] " << error << "\n";
    });

    QObject::connect(&vkClient, &VkApiClient::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired or invalid. Re-authenticating...");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел. Открываем окно входа...\n> ";
        authManager.Authenticate(VK_APP_ID);
    });

    // --- ПЕРВИЧНЫЙ ЗАПУСК ---
    std::string savedToken = authManager.GetSavedToken();
    if (savedToken.empty()) {
        Logger::Log(LogLevel::INFO, "Main: No saved token found. Starting WebView Auth flow...");
        authManager.Authenticate(VK_APP_ID);
    } else {
        Logger::Log(LogLevel::INFO, "Main: Found saved token. Fetching audio...");
        vkClient.SetAccessToken(savedToken);
        vkClient.FetchAllUserAudio(0, 200);
    }

    // --- ЧИСТЫЙ КОНСОЛЬНЫЙ ВВОД (только команды плеера) ---
    std::thread inputThread([&]() {
        std::string input;
        bool isRunning = true;

        while (isRunning) {
            std::cin >> input;
            if (input.empty()) continue;

            char command = std::tolower(input[0]);
            switch (command) {
                case 'p': QMetaObject::invokeMethod(&app, [&]() { if (audio.IsPlaying()) audio.Pause(); else audio.Resume(); }, Qt::QueuedConnection); break;
                case 'n': QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection); break;
                case 'b': QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection); break;
                case '+': QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                case '-': QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                case 's': QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleShuffle(); }, Qt::QueuedConnection); break;
                case 'r': QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleRepeat(); }, Qt::QueuedConnection); break;
                case 'j': {
                    int idx;
                    if (std::cin >> idx) {
                        QMetaObject::invokeMethod(&app, [&, idx]() { playlist.JumpTo(idx - 1); }, Qt::QueuedConnection);
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