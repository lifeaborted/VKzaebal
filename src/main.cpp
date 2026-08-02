#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

// Возвращаем графическое приложение, оно необходимо для QDesktopServices
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMetaObject>
#include <QTimer>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/logger/logger.h"

// Состояния нашего умного консольного ввода
enum class ConsoleState {
    COMMAND_MODE,
    WAITING_TOKEN_URL // Режим ожидания ссылки с токеном из системного браузера
};

std::atomic<ConsoleState> currentState(ConsoleState::COMMAND_MODE);

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QGuiApplication app(argc, argv);
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
        currentState = ConsoleState::COMMAND_MODE;
        std::cout << "\n[УСПЕХ] Авторизация пройдена! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    });

    QObject::connect(&authManager, &VkAuthManager::AuthFailed, [&](const std::string& error) {
        std::cout << "\n[ОШИБКА АВТОРИЗАЦИИ] " << error << "\n";
        std::cout << "Перезапустите приложение для повторной попытки.\n> ";
        currentState = ConsoleState::COMMAND_MODE;
    });

    QObject::connect(&vkClient, &VkApiClient::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired or invalid.");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел. Удалите файл .vk_token и перезапустите плеер.\n> ";
        currentState = ConsoleState::COMMAND_MODE;
    });

    // --- ПЕРВИЧНЫЙ ЗАПУСК ---
    std::string savedToken = "";

    if (const char* envToken = std::getenv("VK_TOKEN")) {
        savedToken = envToken;
        Logger::Log(LogLevel::INFO, "Main: Found token in environment variable VK_TOKEN.");
    } else {
        savedToken = authManager.GetSavedToken();
        if (!savedToken.empty()) {
            Logger::Log(LogLevel::INFO, "Main: Found saved token in file.");
        }
    }

    if (savedToken.empty()) {
        Logger::Log(LogLevel::INFO, "Main: No token found. Opening default OS browser...");
        std::cout << "\n=== Авторизация ВКонтакте ===\n";
        std::cout << "Сейчас откроется ваш браузер по умолчанию. Нажмите 'Продолжить как...',\n";
        std::cout << "затем СКОПИРУЙТЕ всю ссылку из адресной строки пустой страницы\n";
        std::cout << "и вставьте ее сюда:\n\n> ";

        // Открываем системный браузер
        QString authUrl = "https://id.vk.ru/auth?return_auth_hash=09186539220f2b7497&redirect_uri=https%3A%2F%2Foauth.vk.ru%2Fblank.html&redirect_uri_hash=0b961e14fbf361d5d9&force_hash=1&app_id=6287487&response_type=token&code_challenge=&code_challenge_method=&scope=408861919&state=";
        QDesktopServices::openUrl(QUrl(authUrl));

        currentState = ConsoleState::WAITING_TOKEN_URL;
    } else {
        vkClient.SetAccessToken(savedToken);
        currentState = ConsoleState::COMMAND_MODE;
        std::cout << "\n[УСПЕХ] Токен найден! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    }

    // --- КОНСОЛЬНЫЙ ВВОД ---
    std::thread inputThread([&]() {
        std::string input;
        bool isRunning = true;

        while (isRunning) {
            std::cin >> input;
            if (input.empty()) continue;

            if (currentState == ConsoleState::WAITING_TOKEN_URL) {
                QString urlStr = QString::fromStdString(input);
                std::cout << "Обработка ссылки...\n";

                QMetaObject::invokeMethod(&app, [&authManager, urlStr]() {
                    authManager.onUrlIntercepted(urlStr);
                }, Qt::QueuedConnection);

                // Переводим консоль в режим блокировки до ответа
                currentState = ConsoleState::COMMAND_MODE;
                continue;
            }

            // Команды плеера
            if (currentState == ConsoleState::COMMAND_MODE) {
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
        }
    });

    int exitCode = app.exec();
    if (inputThread.joinable()) inputThread.join();

    Logger::Close();
    return exitCode;
}