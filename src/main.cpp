#include <iostream>
#include <string>
#include <atomic>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/ConsoleController/ConsoleController.h"
#include "utils/logger/logger.h"
#include "utils/DatabaseManager/DatabaseManager.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    QGuiApplication app(argc, argv);
    Logger::Init();
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");

    QSettings settings("config.ini", QSettings::IniFormat);

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;

    AudioEngine audio;
    if (!audio.Init()) return -1;

PlaylistManager playlist;
    VkApiClient vkClient;
    VkAuthManager authManager;

    bool gaplessEnabled = settings.value("Audio/GaplessPlayback", true).toBool();
    Track preloadedTrack;

    ConsoleController console(audio, playlist, authManager, dbManager);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    audio.OnTrackFinished = [&]() { playlist.Next(); };

    audio.OnTrackNearEnd = [&]() {
        if (!gaplessEnabled) return;

        Track nextTrack = playlist.PeekNextTrack();
        if (nextTrack.id.empty()) return;

        // Запрашиваем URL за 10 секунд до конца текущего трека
        vkClient.FetchTrackUrl(nextTrack.id, [nextTrack, &audio, &preloadedTrack](const std::string& freshUrl, bool isNetworkError) {
            if (!isNetworkError && !freshUrl.empty()) {
                if (audio.PreloadStream(freshUrl)) {
                    preloadedTrack = nextTrack;
                }
            }
        });
    };

    std::atomic<int> playbackGeneration{0};

    std::function<void(Track, int)> attemptPlay;
    attemptPlay = [&](Track track, int attempt) {
        int currentGen = (attempt == 1) ? ++playbackGeneration : playbackGeneration.load();

        // --- GAPLESS PLAYBACK INTERCEPT ---
        // Если трек уже скачан в фоне, мгновенно запускаем его без лишних запросов к API ВК!
        if (gaplessEnabled && attempt == 1 && audio.HasPreloadedStream() && preloadedTrack.id == track.id) {
            if (audio.PlayPreloaded()) {
                std::cout << "\r                                                                                \r";
                std::cout << track.artist << " - " << track.title
                          << " [" << track.GetFormattedDuration() << "]\n> ";
                std::cout.flush();
                return;
            }
        }
        // ----------------------------------

        auto executePlay = [track, attempt, currentGen, &audio, &playlist, &vkClient, &attemptPlay, &playbackGeneration](const std::string& freshUrl, bool isNetworkError) {
            if (currentGen != playbackGeneration.load()) return;

            if (!isNetworkError && freshUrl.empty()) {
                Logger::Log(LogLevel::WARNING, "Track is restricted or deleted by copyright. Skipping...");
                playlist.Next();
                return;
            }

            bool playSuccess = false;
            if (!freshUrl.empty()) {
                playSuccess = audio.PlayStream(freshUrl);
            }

            if (playSuccess) {
                std::cout << "\r                                                                                \r";
                std::cout << track.artist << " - " << track.title
                          << " [" << track.GetFormattedDuration() << "]\n> ";
                std::cout.flush();
                return;
            }

            Logger::Log(LogLevel::WARNING, "Playback failed for track: " + track.title);
            if (attempt < 3) {
                Logger::Log(LogLevel::INFO, "Retrying stream in 2 seconds...");
                QTimer::singleShot(2000, [track, attempt, &attemptPlay]() { attemptPlay(track, attempt + 1); });
            } else {
                Logger::Log(LogLevel::ERROR, "Max retries reached. Moving to next track.");
                playlist.Next();
            }
        };

        // Если предзагрузки не было (например, при ручном скипе трека), грузим по старинке
        vkClient.FetchTrackUrl(track.id, executePlay);
    };

    playlist.OnTrackRequested = [&](Track track) { attemptPlay(track, 1); };

    // --- ЛОГИКА СКАЧИВАНИЯ СПИСКА ---
    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        for (const auto& track : tracks) playlist.AddTrack(track);

        dbManager.SaveTracks(tracks);
        dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
        // Здесь dbManager.ExportQueueToTxt ВЫКЛЮЧЕН
    });

    // --- ЛОГИКА ЗАВЕРШЕНИЯ ЗАГРУЗКИ ---
    QObject::connect(&vkClient, &VkApiClient::FinishedFetching, [&]() {
        Logger::Log(LogLevel::INFO, "=== ВСЕ ДОСТУПНЫЕ ТРЕКИ УСПЕШНО ЗАГРУЖЕНЫ ===");

        // Записываем плейлист ОДИН раз, когда все треки получены
        dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());

        if (playlist.HasTracks()) {
            std::cout << "\r                                                                                \r"
                      << "=== ВСЕ ТРЕКИ ЗАГРУЖЕНЫ ===\n"
                      << "Введите 'h' для вывода списка команд\n> ";
            std::cout.flush();

            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    // --- ЛОГИКА АВТОРИЗАЦИИ ---
    QObject::connect(&authManager, &VkAuthManager::TokenReceived, [&](const std::string& token) {
        settings.setValue("vk_token", QString::fromStdString(token));
        vkClient.SetAccessToken(token);
        console.SetState(ConsoleState::COMMAND_MODE);
        std::cout << "\n[УСПЕХ] Авторизация пройдена! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    });

    std::function<void()> startAuthFlow = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow. Opening browser...");
        std::cout << "\n=== Авторизация ВКонтакте ===\n";
        std::cout << "СКОПИРУЙТЕ всю ссылку из адресной строки пустой страницы и вставьте ее сюда:\n> ";

        QString authUrl = "https://id.vk.ru/auth?return_auth_hash=8f84ec1a42e15d06f3&redirect_uri=https%3A%2F%2Foauth.vk.ru%2Fblank.html&redirect_uri_hash=840d020814e3175427&force_hash=1&app_id=6287487&response_type=token&code_challenge=&code_challenge_method=&scope=408861919&state=";
        QDesktopServices::openUrl(QUrl(authUrl));
        console.SetState(ConsoleState::WAITING_TOKEN_URL);
    };

    QObject::connect(&vkClient, &VkApiClient::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired.");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел.\n";
        settings.remove("vk_token");
        vkClient.SetAccessToken("");
        startAuthFlow();
    });

    // --- ПЕРВИЧНЫЙ ЗАПУСК ---
    QString savedToken = settings.value("vk_token", "").toString();

    if (savedToken.isEmpty()) {
        startAuthFlow();
    } else {
        vkClient.SetAccessToken(savedToken.toStdString());
        std::cout << "Проверка сохраненного токена...\n";

        vkClient.ValidateToken([&, startAuthFlow](bool isValid) {
            if (isValid) {
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[УСПЕХ] Токен действителен! Загрузка аудио...\n> ";
                vkClient.FetchAllUserAudio(0, 200);
            } else {
                std::cout << "\n[ВНИМАНИЕ] Сохраненный токен недействителен.\n";
                settings.remove("vk_token");
                vkClient.SetAccessToken("");
                startAuthFlow();
            }
        });
    }

    console.Start();
    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}