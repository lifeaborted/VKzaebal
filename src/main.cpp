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

        // Обрати внимание: добавлен параметр bool isNetworkError
        vkClient.FetchTrackUrl(track.id, [&audio, &playlist, track, attempt, &attemptPlay](const std::string& freshUrl, bool isNetworkError) {

            // 1. Трек изъят правообладателем или удален (сеть есть, API ответил успехом, но URL пустой)
            if (!isNetworkError && freshUrl.empty()) {
                Logger::Log(LogLevel::WARNING, "Track is restricted or deleted by copyright. Skipping immediately.");
                playlist.Next();
                return; // Моментально уходим, ретраи не нужны
            }

            // 2. Пытаемся запустить поток BASS (если URL получен)
            bool playSuccess = false;
            if (!freshUrl.empty()) {
                playSuccess = audio.PlayStream(freshUrl);
            }

            // 3. Успешный запуск трека
            if (playSuccess) {
                Logger::Log(LogLevel::INFO, ">>> Playing: " + track.artist + " - " + track.title + " [" + track.GetFormattedDuration() + "]");
                return;
            }

            // 4. Ошибка (таймаут сети VK API, либо отвал BASS при подключении к CDN)
            Logger::Log(LogLevel::WARNING, "Playback or network failed for track: " + track.title);

            if (attempt < 3) {
                Logger::Log(LogLevel::INFO, "Retrying in 2 seconds...");
                QTimer::singleShot(2000, [track, attempt, &attemptPlay]() { attemptPlay(track, attempt + 1); });
            } else {
                Logger::Log(LogLevel::ERROR, "Max retries reached. Skipping track.");
                playlist.Next();
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

    // Лямбда для запуска процесса авторизации
    std::function<void()> startAuthFlow = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow. Opening default OS browser...");
        std::cout << "\n=== Авторизация ВКонтакте ===\n";
        std::cout << "Сейчас откроется ваш браузер по умолчанию. Нажмите 'Продолжить как...',\n";
        std::cout << "затем СКОПИРУЙТЕ всю ссылку из адресной строки пустой страницы\n";
        std::cout << "и вставьте ее сюда:\n\n> ";

        QString authUrl = "https://oauth.vk.com/authorize?client_id=6121396&scope=audio,offline,friends,groups,wall&response_type=token&display=page&redirect_uri=https://oauth.vk.com/blank.html";
        QDesktopServices::openUrl(QUrl(authUrl));

        currentState = ConsoleState::WAITING_TOKEN_URL;
    };

    // Обновленная обработка протухшего токена прямо во время работы
    QObject::connect(&vkClient, &VkApiClient::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired during playback.");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел.\n";
        authManager.ClearSavedToken();
        vkClient.SetAccessToken("");
        startAuthFlow();
    });

    // Логика запуска с валидацией
    if (savedToken.empty()) {
        startAuthFlow();
    } else {
        vkClient.SetAccessToken(savedToken);
        std::cout << "Проверка сохраненного токена...\n";

        vkClient.ValidateToken([&, startAuthFlow](bool isValid) {
            if (isValid) {
                currentState = ConsoleState::COMMAND_MODE;
                std::cout << "\n[УСПЕХ] Токен действителен! Загрузка аудио...\n> ";
                vkClient.FetchAllUserAudio(0, 200);
            } else {
                std::cout << "\n[ВНИМАНИЕ] Сохраненный токен недействителен. Запуск новой авторизации...\n";
                authManager.ClearSavedToken();
                vkClient.SetAccessToken("");
                startAuthFlow();
            }
        });
    }

// --- КОНСОЛЬНЫЙ ВВОД ---
    std::thread inputThread([&]() {
        std::string input;
        bool isRunning = true;

        while (isRunning) {
            // Заменяем cin на getline, чтобы читать строку целиком вместе с пробелами
            std::getline(std::cin, input);
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
                // 1. Команда изменения громкости (например: "v 50")
                if (input.length() >= 3 && std::tolower(input[0]) == 'v' && input[1] == ' ') {
                    try {
                        int volTarget = std::stoi(input.substr(2));
                        if (volTarget >= 0 && volTarget <= 100) {
                            float normalizedVol = volTarget / 100.0f;
                            QMetaObject::invokeMethod(&app, [&, normalizedVol]() {
                                audio.SetVolume(normalizedVol);
                            }, Qt::QueuedConnection);
                        } else {
                            std::cout << "[Ошибка] Введите значение от 0 до 100 (например: v 50)\n> ";
                        }
                    } catch (...) {
                        std::cout << "[Ошибка] Неверный формат числа для громкости.\n> ";
                    }
                    continue;
                }

                // 2. Команда прыжка к треку (например: "j 14")
                if (input.length() >= 3 && std::tolower(input[0]) == 'j' && input[1] == ' ') {
                    try {
                        int idx = std::stoi(input.substr(2));
                        QMetaObject::invokeMethod(&app, [&, idx]() { playlist.JumpTo(idx - 1); }, Qt::QueuedConnection);
                    } catch (...) {
                        std::cout << "[Ошибка] Неверный номер трека.\n> ";
                    }
                    continue;
                }

                // 3. Старые односимвольные команды
                char command = std::tolower(input[0]);
                switch (command) {
                    case 'p': QMetaObject::invokeMethod(&app, [&]() { if (audio.IsPlaying()) audio.Pause(); else audio.Resume(); }, Qt::QueuedConnection); break;
                    case 'n': QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection); break;
                    case 'b': QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection); break;
                    case '+': QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                    case '-': QMetaObject::invokeMethod(&app, [&]() { audio.SetVolume(audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                    case 's': QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleShuffle(); }, Qt::QueuedConnection); break;
                    case 'r': QMetaObject::invokeMethod(&app, [&]() { playlist.ToggleRepeat(); }, Qt::QueuedConnection); break;
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