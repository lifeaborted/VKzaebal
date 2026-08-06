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

#include "core/audio/bass/BassEngine.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/api/Client.h"
#include "core/vk/auth/Manager.h"
#include "ui/console/ConsoleController.h"
#include "utils/logger/Logger.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
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

    BassEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    Client vkClient;
    Manager authManager;
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;

    bool crossfadeEnabled = settings.value("Audio/CrossfadePlayback", false).toBool();
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();
    bool autoPlay = settings.value("Session/AutoPlay", false).toBool();

    playlist.SetShuffle(isShuffle); // Применяем шафл к плейлисту
    Track preloadedTrack;
    std::string cachedNextUrl = "";

    float savedVolume = settings.value("Session/Volume", 1.0f).toFloat();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();
    double savedPosition = settings.value("Session/Position", 0.0).toDouble();

    audio.SetVolume(savedVolume);

    std::vector<Track> cachedTracks = dbManager.LoadTracks();
    bool isPlaybackStarted = false;

    ConsoleController console(audio, playlist, authManager, dbManager, vkClient, downloader, lyricsFetcher);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    // Умная инициализация плейлиста
    auto initPlaylistAndStart = [&](bool isOnline) {
        if (isPlaybackStarted) return;

        if (!playlist.HasTracks()) {
            if (isOnline) {
                // в онлайнк загружаем всё
                for (const auto& t : cachedTracks) playlist.AddTrack(t);
            } else {
                // в оффлайне только локальные файлы
                for (const auto& t : cachedTracks) {
                    QString path = "downloads/" + QString::fromStdString(t.GetSafeFilename()) + ".mp3";
                    if (QFile::exists(path)) {
                        playlist.AddTrack(t);
                    }
                }
            }
        }

        if (playlist.HasTracks()) {
            std::cout << "\r\033[2K"
                      << "=== ПЛЕЕР ГОТОВ К РАБОТЕ ===\n"
                      << "Режим очереди: " << (isShuffle ? "Шафл (Случайный)" : "Стандартный") << "\n"
                      << "Автостарт: " << (autoPlay ? "ВКЛ" : "ВЫКЛ") << "\n"
                      << (isOnline ? "" : "[ОФФЛАЙН] Загружены только скачанные треки.\n")
                      << "Введите 'h' для вывода списка команд\n\n> ";
            std::cout.flush();

            isPlaybackStarted = true;

            if (savedTrackIndex >= 0 && savedTrackIndex < playlist.GetAllTracks().size()) {
                playlist.JumpTo(savedTrackIndex);
            } else {
                playlist.OnTrackRequested(playlist.GetCurrentTrack());
            }

            // Если автоплей выключен - сразу ставим на паузу
            if (!autoPlay) {
                audio.Pause();
            }
        } else {
            std::cout << "\n[Оффлайн] Нет скачанных треков. Плеер пуст.\n> ";
            std::cout.flush();
        }
    };

    QObject::connect(&console, &ConsoleController::OfflineModeRequested, [&]() {
        initPlaylistAndStart(false); // Запуск строго в оффлайн режиме
    });

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

    int skipCount = 0;
    std::atomic<int> playbackGeneration{0};
    std::function<void(Track, int)> attemptPlay;

    attemptPlay = [&](Track track, int attempt) {
        int currentGen = (attempt == 1) ? ++playbackGeneration : playbackGeneration.load();

        QString localPath = "downloads/" + QString::fromStdString(track.GetSafeFilename()) + ".mp3";
        bool isDownloaded = QFile::exists(localPath);

        if (attempt == 1 && !cachedNextUrl.empty() && preloadedTrack.id == track.id && !isDownloaded) {
            if (audio.PlayStream(cachedNextUrl, track.duration, crossfadeEnabled, track.GetSafeFilename())) {
                cachedNextUrl = "";
                skipCount = 0;
                if (savedPosition > 0.0) {
                    audio.SetPositionSeconds(savedPosition);
                    savedPosition = 0.0;
                }
                std::cout << "\r\033[2K\033[1A\r\033[2K\n> ";
                std::cout.flush();
                return;
            }
        } else if (attempt == 1) {
            std::cout << "\r\033[2K\033[1A\r\033[2K";
            std::cout << "[Загрузка] " << track.artist << " - " << track.title << "...\n\n> ";
            std::cout.flush();
        }

        if (isDownloaded) {
            skipCount = 0;
            if (audio.PlayStream("", track.duration, crossfadeEnabled, track.GetSafeFilename())) {
                if (savedPosition > 0.0) {
                    audio.SetPositionSeconds(savedPosition);
                    savedPosition = 0.0;
                }
                std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
                std::cout.flush();
            } else {
                playlist.Next();
            }
            return;
        }

        auto executePlay = [track, attempt, currentGen, &audio, &playlist, &vkClient, &attemptPlay, &playbackGeneration, &savedPosition, &crossfadeEnabled, &skipCount](const std::string& freshUrl, bool isNetworkError) {
            if (currentGen != playbackGeneration.load()) return;

            if (!isNetworkError && freshUrl.empty()) {
                skipCount++;
                // Если онлайн сессия сломалась (пропал инет) и скипнуто 5 треков подряд - стопаем
                if (skipCount >= 5) {
                    Logger::Log(LogLevel::ERROR, "Too many unplayable tracks. Stopping loop.");
                    skipCount = 0;
                    std::cout << "\r\033[2K\033[1A\r\033[2K[Внимание] Ошибка сети/токена. Воспроизведение остановлено.\n\n> ";
                    std::cout.flush();
                    return;
                }

                Logger::Log(LogLevel::WARNING, "Track is restricted or token invalid. Skipping...");
                playlist.Next();
                return;
            }

            skipCount = 0;

            if (!freshUrl.empty() && audio.PlayStream(freshUrl, track.duration, crossfadeEnabled, track.GetSafeFilename())) {
                if (savedPosition > 0.0) {
                    audio.SetPositionSeconds(savedPosition);
                    savedPosition = 0.0;
                }
                std::cout << "\r\033[2K\033[1A\r\033[2K\033[1A\r\033[2K\n> ";
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

    QObject::connect(&vkClient, &Client::AudioFetched, [&](const std::vector<Track>& tracks) {
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
            initPlaylistAndStart(true);
            dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
        } else if (hasNewTracks) {
            dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
            dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());
        }
    });

    QObject::connect(&vkClient, &Client::FinishedFetching, [&]() {
        Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
    });

    QObject::connect(&authManager, &Manager::TokenReceived, [&](const std::string& token) {
        settings.setValue("vk_token", QString::fromStdString(token));
        vkClient.SetAccessToken(token);
        console.SetState(ConsoleState::COMMAND_MODE);
        std::cout << "\n[УСПЕХ] Авторизация пройдена! Загрузка аудио...\n> ";
        std::cout.flush();

        initPlaylistAndStart(true);
        vkClient.FetchAllUserAudio(0, 200);
    });

    std::function<void()> startAuthFlow = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow...");
        std::cout << "\n=== Авторизация ВКонтакте ===\n";
        std::cout << "СКОПИРУЙТЕ всю ссылку из пустой страницы и вставьте ее сюда\n";
        std::cout << "(Или введите 'offline' для запуска без интернета):\n> ";
        std::cout.flush();

        QString authUrl = "https://id.vk.ru/auth?return_auth_hash=8f84ec1a42e15d06f3&redirect_uri=https%3A%2F%2Foauth.vk.ru%2Fblank.html&redirect_uri_hash=840d020814e3175427&force_hash=1&app_id=6287487&response_type=token&code_challenge=&code_challenge_method=&scope=408861919&state=";
        QDesktopServices::openUrl(QUrl(authUrl));
        console.SetState(ConsoleState::WAITING_TOKEN_URL);
    };

    QObject::connect(&vkClient, &Client::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired.");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел.\n";
        settings.remove("vk_token");
        vkClient.SetAccessToken("");
        startAuthFlow();
    });

    QString savedToken = settings.value("vk_token", "").toString();

    if (savedToken.isEmpty()) {
        startAuthFlow();
    } else {
        vkClient.SetAccessToken(savedToken.toStdString());
        std::cout << "Проверка сохраненного токена...\n";
        std::cout.flush();

        vkClient.ValidateToken([&, startAuthFlow, initPlaylistAndStart](bool isValid) {
            if (isValid) {
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[УСПЕХ] Синхронизация свежих треков...\n> ";
                std::cout.flush();

                initPlaylistAndStart(true);
                vkClient.FetchAllUserAudio(0, 200);
            } else {
                std::cout << "\n[ВНИМАНИЕ] Нет сети или токен недействителен.\n";
                std::cout.flush();
                settings.remove("vk_token");
                vkClient.SetAccessToken("");
                startAuthFlow();
            }
        });
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        Logger::Log(LogLevel::INFO, "Main: Saving session state...");
        settings.setValue("Session/Volume", audio.GetVolume());
        settings.setValue("Session/CurrentTrackIndex", playlist.GetCurrentAbsoluteIndex());
        settings.setValue("Session/Position", audio.GetPositionSeconds());
        settings.setValue("Session/Shuffle", playlist.IsShuffle());
        settings.sync();
    });

    console.Start();
    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}