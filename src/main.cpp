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
#include "utils/TrackDownloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"


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
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;

    bool crossfadeEnabled = settings.value("Audio/CrossfadePlayback", false).toBool();
    Track preloadedTrack;
    std::string cachedNextUrl = "";

float savedVolume = settings.value("Session/Volume", 1.0f).toFloat();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();
    double savedPosition = settings.value("Session/Position", 0.0).toDouble();

    audio.SetVolume(savedVolume);

    // --- ЗАГРУЗКА ИЗ КЭША ---
    std::vector<Track> cachedTracks = dbManager.LoadTracks();
    for (const auto& t : cachedTracks) {
        playlist.AddTrack(t);
    }
    bool isCacheLoaded = !cachedTracks.empty();
    bool isPlaybackStarted = false;

    auto startPlayback = [&]() {
        if (isPlaybackStarted) return;
        isPlaybackStarted = true;

        if (playlist.HasTracks()) {
            std::cout << "\r                                                                                \r"
                      << "=== ПЛЕЕР ГОТОВ К РАБОТЕ ===\n"
                      << "Введите 'h' для вывода списка команд\n> ";
            std::cout.flush();

            if (savedTrackIndex >= 0 && savedTrackIndex < playlist.GetAllTracks().size()) {
                playlist.JumpTo(savedTrackIndex);
            } else {
                playlist.OnTrackRequested(playlist.GetCurrentTrack());
            }
        }
    };

    ConsoleController console(audio, playlist, authManager, dbManager, vkClient, downloader, lyricsFetcher);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    console.OnGaplessModeChanged = [&](bool isCrossfade) {
        crossfadeEnabled = isCrossfade;
        settings.setValue("Audio/CrossfadePlayback", isCrossfade);
        settings.sync();
        Logger::Log(LogLevel::INFO, std::string("Main: Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    audio.OnTrackFinished = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Auto-switching to next track...");
        playlist.Next();
    };

    // ФОНОВАЯ ЗАГРУЗКА URL
    audio.OnTrackNearEnd = [&]() {
        Track nextTrack = playlist.PeekNextTrack();
        if (nextTrack.id.empty()) return;

        vkClient.FetchTrackUrl(nextTrack.id, [nextTrack, &cachedNextUrl, &preloadedTrack](const std::string& freshUrl, bool isNetworkError) {
            if (!isNetworkError && !freshUrl.empty()) {
                cachedNextUrl = freshUrl;
                preloadedTrack = nextTrack;
                Logger::Log(LogLevel::INFO, "Main: Next track URL pre-fetched successfully in background.");
            }
        });
    };

    std::atomic<int> playbackGeneration{0};
    std::function<void(Track, int)> attemptPlay;

    attemptPlay = [&](Track track, int attempt) {
        int currentGen = (attempt == 1) ? ++playbackGeneration : playbackGeneration.load();

        if (attempt == 1 && !cachedNextUrl.empty() && preloadedTrack.id == track.id) {
            if (audio.PlayStream(cachedNextUrl, track.duration, crossfadeEnabled, track.id)) {
                cachedNextUrl = "";
                if (savedPosition > 0.0) {
                    audio.SetPositionSeconds(savedPosition);
                    savedPosition = 0.0;
                }
                std::cout << "\r                                                                                \r";
                std::cout << track.artist << " - " << track.title << " [" << track.GetFormattedDuration() << "]\n> ";
                std::cout.flush();
                return;
            }
        } else if (attempt == 1) {
            std::cout << "\r                                                                                \r";
            std::cout << "[Загрузка] " << track.artist << " - " << track.title << "...\n> ";
            std::cout.flush();
        }

        auto executePlay = [track, attempt, currentGen, &audio, &playlist, &vkClient, &attemptPlay, &playbackGeneration, &savedPosition, &crossfadeEnabled](const std::string& freshUrl, bool isNetworkError) {
            if (currentGen != playbackGeneration.load()) return;

            if (!isNetworkError && freshUrl.empty()) {
                Logger::Log(LogLevel::WARNING, "Track is restricted. Skipping...");
                playlist.Next();
                return;
            }

            if (!freshUrl.empty() && audio.PlayStream(freshUrl, track.duration, crossfadeEnabled, track.id)) {
                if (savedPosition > 0.0) {
                    audio.SetPositionSeconds(savedPosition);
                    savedPosition = 0.0;
                }
                std::cout << "\r                                                                                \r";
                std::cout << track.artist << " - " << track.title << " [" << track.GetFormattedDuration() << "]\n> ";
                std::cout.flush();
                return;
            }

            if (attempt < 3) {
                Logger::Log(LogLevel::INFO, "Retrying stream in 2 seconds...");
                QTimer::singleShot(2000, [track, attempt, &attemptPlay]() { attemptPlay(track, attempt + 1); });
            } else {
                playlist.Next();
            }
        };

        vkClient.FetchTrackUrl(track.id, executePlay);
    };

    playlist.OnTrackRequested = [&](Track track) { attemptPlay(track, 1); };

    // --- ЛОГИКА ФОНОВОЙ СИНХРОНИЗАЦИИ ---
    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        bool hasNewTracks = false;
        auto allTracks = playlist.GetAllTracks();

        for (const auto& track : tracks) {
            bool exists = false;
            for (const auto& cached : allTracks) {
                if (cached.id == track.id) { exists = true; break; }
            }
            if (!exists) {
                playlist.AddTrack(track);
                hasNewTracks = true;
            }
        }

        dbManager.SaveTracks(tracks);

        if (!isPlaybackStarted) {
            startPlayback();
            dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
        } else if (hasNewTracks) {
            dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
        }
    });

    QObject::connect(&vkClient, &VkApiClient::FinishedFetching, [&]() {
        Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
        dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());
    });

    // --- 3. ЛОГИКА АВТОРИЗАЦИИ И СТАРТА ---
    QObject::connect(&authManager, &VkAuthManager::TokenReceived, [&](const std::string& token) {
        settings.setValue("vk_token", QString::fromStdString(token));
        vkClient.SetAccessToken(token);
        console.SetState(ConsoleState::COMMAND_MODE);
        std::cout << "\n[УСПЕХ] Авторизация пройдена! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    });

    std::function<void()> startAuthFlow = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow...");
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

    QString savedToken = settings.value("vk_token", "").toString();

    if (savedToken.isEmpty()) {
        if (isCacheLoaded) {
            std::cout << "\n[Оффлайн] Токена нет, но есть кэш. Запуск оффлайн-режима...\n";
            console.SetState(ConsoleState::COMMAND_MODE);
            startPlayback();
        } else {
            startAuthFlow();
        }
    } else {
        vkClient.SetAccessToken(savedToken.toStdString());
        std::cout << "Проверка сохраненного токена...\n";

        vkClient.ValidateToken([&, startAuthFlow, startPlayback, isCacheLoaded](bool isValid) {
            if (isValid) {
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[УСПЕХ] Синхронизация свежих треков...\n> ";
                vkClient.FetchAllUserAudio(0, 200);

                if (isCacheLoaded) {
                    QTimer::singleShot(2500, [&, startPlayback]() {
                        startPlayback();
                    });
                }
            } else {
                if (isCacheLoaded) {
                    std::cout << "\n[ВНИМАНИЕ] Нет сети или токен недействителен. Оффлайн режим.\n";
                    console.SetState(ConsoleState::COMMAND_MODE);
                    startPlayback();
                } else {
                    std::cout << "\n[ВНИМАНИЕ] Сохраненный токен недействителен.\n";
                    settings.remove("vk_token");
                    vkClient.SetAccessToken("");
                    startAuthFlow();
                }
            }
        });
    }

    // --- СОХРАНЕНИЕ СЕССИИ ПРИ ВЫХОДЕ ---
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        Logger::Log(LogLevel::INFO, "Main: Saving session state...");
        settings.setValue("Session/Volume", audio.GetVolume());
        settings.setValue("Session/CurrentTrackIndex", playlist.GetCurrentAbsoluteIndex());
        settings.setValue("Session/Position", audio.GetPositionSeconds());
        settings.sync();
    });

    console.Start();
    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}