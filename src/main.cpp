#include <iostream>
#include <QGuiApplication>
#include <QSettings>
#include <string>
#include <QtWebView>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/api/soundcloud/SoundCloudClient.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "ui/console/core/ConsoleController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/env/EnvParser.h"
#include "core/audio/playback/PlaybackController.h"

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
    app.setQuitOnLastWindowClosed(false);
    QtWebView::initialize();

    QCoreApplication::setOrganizationName("VKAudioTeam");
    QCoreApplication::setApplicationName("VKAudioPlayer");

    PathManager::Init();
    Logger::Init();

    QMap<QString, QString> envVars = EnvParser::Parse(".env");

    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    if (!settings.contains("Audio/CrossfadeDurationMs")) {
        settings.setValue("Audio/CrossfadeDurationMs", 3000);
        settings.sync();
    }

    std::string activeSource = settings.value("General/source", "VK").toString().toStdString();
    bool crossfadeEnabled = settings.value("Audio/CrossfadePlayback", false).toBool();
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();
    bool autoPlay = settings.value("Session/AutoPlay", false).toBool();
    float savedVolume = settings.value("Session/Volume", 1.0f).toFloat();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();
    double savedPosition = settings.value("Session/Position", 0.0).toDouble();
    int savedRepeatMode = settings.value("Session/Repeat", 1).toInt();

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;

    MiniaudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    playlist.SetRepeatMode(savedRepeatMode);
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;
    NetworkStreamer streamer;

    PlaybackController playbackCtrl(audio, playlist, streamer);
    SourceRouter router(envVars);

    // Заглушка для конструктора консоли (позже нужно убрать authManager оттуда)
    ConsoleController console(audio, playlist, *router.GetAuthManager(), dbManager, downloader, lyricsFetcher);

    audio.SetVolume(savedVolume);
    bool isPlaybackStarted = false;
    int vkSyncIndex = 0;

    playbackCtrl.SetCrossfadeEnabled(crossfadeEnabled);
    playbackCtrl.SetSavedPosition(savedPosition);

    // --- Связи компонентов (Внутренняя логика плеера) ---
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        audio.PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    audio.OnNetworkSeekRequested = [&](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&streamer, targetSeconds]() {
            streamer.SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };

    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    audio.OnTrackFinished = [&]() { playbackCtrl.HandleTrackFinished(); };
    audio.OnTrackNearEnd = [&]() { playbackCtrl.HandleTrackNearEnd(); };
    audio.OnPlaybackError = [&](const std::string& err) {
        Logger::Log(LogLevel::ERROR, "Playback failed: " + err + ". Skipping to next track...");
        playlist.Next();
    };
    playlist.OnTrackRequested = [&](Track track) { playbackCtrl.AttemptPlay(track); };

    console.OnGaplessModeChanged = [&](bool isCrossfade) {
        settings.setValue("Audio/CrossfadePlayback", isCrossfade);
        settings.sync();
        playbackCtrl.SetCrossfadeEnabled(isCrossfade);
        Logger::Log(LogLevel::INFO, std::string("Main: Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    // --- Функции инициализации ---
    auto initPlaylistAndStart = [&](bool isOnline) {
        if (isPlaybackStarted) return;

        if (!playlist.HasTracks()) {
            std::vector<Track> cachedTracks = dbManager.LoadTracks(activeSource);
            if (isOnline) {
                for (const auto& t : cachedTracks) playlist.AddTrack(t);
            } else {
                for (const auto& t : cachedTracks) {
                    QString mp3Path = PathManager::GetDownloadFilePath(t.GetSafeFilename(), "mp3");
                    QString aacPath = PathManager::GetDownloadFilePath(t.GetSafeFilename(), "aac");
                    if (QFile::exists(mp3Path) || QFile::exists(aacPath)) {
                        playlist.AddTrack(t);
                    }
                }
            }

            if (isShuffle) {
                std::vector<std::string> savedQueue = dbManager.LoadQueueIds(activeSource, true);
                if (!savedQueue.empty()) {
                    playlist.RestoreShuffleQueue(savedQueue);
                } else {
                    playlist.SetShuffle(true);
                }
            }
        }

        if (playlist.HasTracks()) {
            std::cout << "\r\033[2K=== ПЛЕЕР ГОТОВ К РАБОТЕ ===\nРежим очереди: " << (isShuffle ? "Шафл" : "Стандартный")
                      << "\nАвтостарт: " << (autoPlay ? "ВКЛ" : "ВЫКЛ") << "\n"
                      << (isOnline ? "" : "[ОФФЛАЙН] Загружены только скачанные треки.\n")
                      << "Введите 'h' для вывода списка команд\n\n> ";
            std::cout.flush();

            isPlaybackStarted = true;

            if (savedTrackIndex >= 0 && savedTrackIndex < playlist.GetAllTracks().size()) {
                playlist.JumpTo(savedTrackIndex);
                savedTrackIndex = -1;
            } else {
                playlist.OnTrackRequested(playlist.GetCurrentTrack());
            }

            if (!autoPlay) audio.Pause();
        } else {
            std::cout << "\n[Оффлайн] Нет скачанных треков. Плеер пуст.\n> ";
            std::cout.flush();
        }
    };

    auto onAudioFetched = [&](const std::vector<Track>& tracks) {
        bool hasNewTracks = false;
        auto allTracks = playlist.GetAllTracks();
        for (const auto& track : tracks) {
            bool exists = false;
            for (const auto& cached : allTracks) {
                if (cached.id == track.id) { exists = true; break; }
            }
            if (!exists) {
                playlist.InsertTrack(vkSyncIndex, track);
                hasNewTracks = true;
            }
            vkSyncIndex++;
        }
        dbManager.SaveTracks(tracks);
        if (!isPlaybackStarted) {
            initPlaylistAndStart(true);
        }
        if (!isPlaybackStarted || hasNewTracks) {
            dbManager.SaveQueue(playlist.GetAllTracks(), activeSource, false);
            dbManager.SaveQueue(playlist.GetQueueTracks(), activeSource, playlist.IsShuffle());
            dbManager.ExportQueueToTxt(playlist.GetQueueTracks(), "playlist.txt", playlist.IsShuffle());
        }
    };

    auto onFinishedFetching = [&]() {
        Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
        dbManager.SaveQueue(playlist.GetAllTracks(), activeSource, false);
        dbManager.SaveQueue(playlist.GetQueueTracks(), activeSource, playlist.IsShuffle());
        dbManager.ExportQueueToTxt(playlist.GetQueueTracks(), "playlist.txt", playlist.IsShuffle());
    };

    // --- Связи с роутером ---
    QObject::connect(&console, &ConsoleController::OfflineModeRequested, &app, [&]() {
        initPlaylistAndStart(false);
    }, Qt::QueuedConnection);

    QObject::connect(&console, &ConsoleController::SourceChanged, &app, [&](const std::string& source) {
        router.SwitchSource(source);
    }, Qt::QueuedConnection);

    QObject::connect(&router, &SourceRouter::SourceChanged, [&](const std::string& newSource) {
        activeSource = newSource;
        playbackCtrl.ClearState();
        isPlaybackStarted = false;
        playlist.Clear();

        settings.setValue("General/source", QString::fromStdString(newSource));
        settings.sync();

        console.SetCurrentProvider(router.GetCurrentProvider());
        playbackCtrl.SetCurrentProvider(router.GetCurrentProvider());
    });

    QObject::connect(&router, &SourceRouter::AuthUiStateChanged, [&](bool isWaiting) {
        console.SetState(isWaiting ? ConsoleState::WAITING_TOKEN_URL : ConsoleState::COMMAND_MODE);
    });

    QObject::connect(&router, &SourceRouter::ProviderReady, [&](bool isOnline) {
        vkSyncIndex = 0;
        initPlaylistAndStart(isOnline);
    });

    QObject::connect(router.GetVkClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetVkClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetVkClient(), &IAudioProvider::FinishedFetching, [&]() {
        if (router.GetCurrentProvider() == router.GetVkClient()) onFinishedFetching();
    });

    QObject::connect(router.GetSpotifyClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetSpotifyClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetSpotifyClient(), &IAudioProvider::FinishedFetching, [&]() {
        if (router.GetCurrentProvider() == router.GetSpotifyClient()) onFinishedFetching();
    });

    QObject::connect(router.GetSoundCloudClient(), &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (router.GetCurrentProvider() == router.GetSoundCloudClient()) onAudioFetched(tracks);
    });
    QObject::connect(router.GetSoundCloudClient(), &IAudioProvider::FinishedFetching, [&]() {
            if (router.GetCurrentProvider() == router.GetSoundCloudClient()) onFinishedFetching();
        });


    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        Logger::Log(LogLevel::INFO, "Main: Saving session state...");
        settings.setValue("Session/Volume", audio.GetVolume());
        settings.setValue("Session/CurrentTrackIndex", playlist.GetCurrentAbsoluteIndex());
        settings.setValue("Session/Position", audio.GetPositionSeconds());
        settings.setValue("Session/Shuffle", playlist.IsShuffle());
        settings.setValue("Session/Repeat", playlist.GetRepeatMode());
        settings.sync();
    });

    router.SwitchSource(activeSource);
    console.Start();

    int exitCode = app.exec();
    Logger::Close();
    return exitCode;
}