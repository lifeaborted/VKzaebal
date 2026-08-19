#include <iostream>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <string>
#include <QtWebView>
#include <QWindow>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/auth/OAuthManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "ui/console/ConsoleController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/env/EnvParser.h"

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

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;
    std::vector<Track> cachedTracks = dbManager.LoadTracks(activeSource);

    MiniaudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    VkClient vkClient;
    SpotifyClient spotifyClient;
    OAuthManager authManager;
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;
    NetworkStreamer streamer;
    IAudioProvider* currentProvider = nullptr;



    Track preloadedTrack;
    std::string cachedNextUrl = "";


    audio.SetVolume(savedVolume);

    bool isPlaybackStarted = false;
    int vkSyncIndex = 0;

    // --- Связи компонентов ---
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        audio.PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    audio.OnNetworkSeekRequested = [&](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&streamer, targetSeconds]() {
            streamer.SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };

    ConsoleController console(audio, playlist, authManager, dbManager, downloader, lyricsFetcher);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    // --- Инициализация плейлиста ---
    auto initPlaylistAndStart = [&](bool isOnline) {
        if (isPlaybackStarted) return;

        if (!playlist.HasTracks()) {
            cachedTracks = dbManager.LoadTracks(activeSource);

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
            if (isShuffle) playlist.SetShuffle(true);
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

    QObject::connect(&console, &ConsoleController::OfflineModeRequested, &app, [&]() {
        initPlaylistAndStart(false);
    }, Qt::QueuedConnection);

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

        if (!currentProvider) return;
        currentProvider->FetchTrackUrl(nextTrack.id, [nextTrack, &cachedNextUrl, &preloadedTrack](const std::string& freshUrl, bool isNetworkError) {
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

        QString localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "mp3");
        if (!QFile::exists(localPath)) {
            localPath = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "aac");
        }
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

        auto executePlay = [track, attempt, currentGen, &audio, &playlist, &vkClient, &attemptPlay, &playbackGeneration, &savedPosition, &crossfadeEnabled, &skipCount, &streamer](const std::string& freshUrl, bool isNetworkError) {
            if (currentGen != playbackGeneration.load()) return;

            if (!isNetworkError && freshUrl.empty()) {
                skipCount++;
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

            if (!freshUrl.empty()) {
                streamer.StopDownload();

                // Очищаем состояние движка перед новым треком
                audio.ClearBuffers(crossfadeEnabled, track.duration);

                // Запускаем скачивание нового трека
                streamer.StartDownload(freshUrl);
                audio.Resume();

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

        if (currentProvider) {
            currentProvider->FetchTrackUrl(track.id, executePlay);
        } else if (!isDownloaded) {
            playlist.Next();
        }
    };

    playlist.OnTrackRequested = [&](Track track) { attemptPlay(track, 1); };

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

    QObject::connect(&vkClient, &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (currentProvider == &vkClient) onAudioFetched(tracks);
    });
    QObject::connect(&spotifyClient, &IAudioProvider::AudioFetched, [&](const std::vector<Track>& tracks) {
        if (currentProvider == &spotifyClient) onAudioFetched(tracks);
    });

    QObject::connect(&vkClient, &IAudioProvider::FinishedFetching, [&]() {
        if (currentProvider == &vkClient) onFinishedFetching();
    });
    QObject::connect(&spotifyClient, &IAudioProvider::FinishedFetching, [&]() {
        if (currentProvider == &spotifyClient) onFinishedFetching();
    });

    // === АВТОРИЗАЦИЯ И РОУТИНГ ===
    QQmlApplicationEngine* authEngine = nullptr;
    QString currentAuthService = "";

    auto startAuthFlow = [&](const QString& service, const QString& authUrl) {
        currentAuthService = service;
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow via QML for " + service.toStdString() + "...");
        console.SetState(ConsoleState::WAITING_TOKEN_URL);

        std::cout << "\n=== Авторизация " << service.toStdString() << " ===\n";
        std::cout << "Откроется окно браузера. Войдите в аккаунт, токен перехватится автоматически.\n> ";
        std::cout.flush();

        if (!authEngine) {
            authEngine = new QQmlApplicationEngine();
            authEngine->rootContext()->setContextProperty("cppAuthManager", &authManager);
            authEngine->rootContext()->setContextProperty("cppAuthUrl", authUrl);
            authEngine->load(QUrl(QStringLiteral("qrc:/core/auth/auth.qml")));

            if (authEngine->rootObjects().isEmpty()) {
                Logger::Log(LogLevel::ERROR, "Main: Failed to load auth.qml!");
            }
            else {
                QWindow* rootWindow = qobject_cast<QWindow*>(authEngine->rootObjects().first());
                if (rootWindow) {
                    QObject::connect(rootWindow, &QWindow::visibleChanged, &app, [&](bool visible) {
                        if (!visible && authEngine) {
                            authEngine->deleteLater();
                            authEngine = nullptr;
                            console.SetState(ConsoleState::COMMAND_MODE);
                            std::cout << "\n[Инфо] Окно авторизации закрыто.\n> ";
                            std::cout.flush();
                        }
                    });
                }
            }
        }
    };

    // Коннект: ВК (Токен из URL)
    QObject::connect(&authManager, &OAuthManager::TokenReceived, [&](const std::string& token) {
        if (authEngine) { authEngine->deleteLater(); authEngine = nullptr; }

        authManager.SaveToken(token, currentAuthService);
        console.SetState(ConsoleState::COMMAND_MODE);

        std::cout << "\n[УСПЕХ] Авторизация " << currentAuthService.toStdString() << " пройдена!\n> ";
        std::cout.flush();

        if (currentAuthService == "VK") {
            vkClient.SetAccessToken(token);
            initPlaylistAndStart(true);
            vkClient.FetchAllUserAudio(0, 200);
        }
    });

    // Коннект: Spotify


    // Успешный обмен кода на токен (Spotify)
    QObject::connect(&spotifyClient, &SpotifyClient::TokenReceived, [&](const std::string& token) {
        authManager.SaveToken(token, "Spotify");
        console.SetState(ConsoleState::COMMAND_MODE);
        std::cout << "\n[УСПЕХ] Авторизация Spotify пройдена!\n> ";
        std::cout.flush();

        spotifyClient.SetAccessToken(token);
        initPlaylistAndStart(true);
        vkSyncIndex = 0;
        spotifyClient.FetchAllUserAudio(0, 50);
    });

    QObject::connect(&spotifyClient, &SpotifyClient::AuthError, [&](const std::string& err) {
        std::cout << "\n[ОШИБКА] Не удалось получить токен Spotify: " << err << "\n> ";
        std::cout.flush();
        console.SetState(ConsoleState::COMMAND_MODE);
    });

    QObject::connect(&vkClient, &VkClient::TokenExpired, [&]() {
        if (console.GetState() == ConsoleState::WAITING_TOKEN_URL) return;
        Logger::Log(LogLevel::WARNING, "Main: Token VK expired.");
        std::cout << "\n[ВНИМАНИЕ] Токен ВК устарел.\n";
        authManager.ClearSavedToken("VK");
        vkClient.SetAccessToken("");
        startAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
    });

    // === Роутер сервисов ===
    auto startVkService = [&]() {
        std::string savedToken = authManager.GetSavedToken("VK");
        if (savedToken.empty()) {
            startAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
        } else {
            vkClient.SetAccessToken(savedToken);
            vkClient.ValidateToken([&, startAuthFlow, initPlaylistAndStart](bool isValid) {
                if (isValid) {
                    console.SetState(ConsoleState::COMMAND_MODE);
                    initPlaylistAndStart(true);
                    vkSyncIndex = 0;
                    vkClient.FetchAllUserAudio(0, 200);
                } else {
                    authManager.ClearSavedToken("VK");
                    vkClient.SetAccessToken("");
                    startAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
                }
            });
        }
    };

    auto startSpotifyService = [&]() {
        QString spDc = envVars.value("SPOTIFY_SP_DC", "");
        if (spDc.isEmpty()) {
            console.SetState(ConsoleState::COMMAND_MODE);
            std::cout << "\n[ОШИБКА] SPOTIFY_SP_DC не задан в .env файле!\n> "; std::cout.flush();
            return;
        }

        std::string savedToken = authManager.GetSavedToken("Spotify");
        if (savedToken.empty()) {
            std::cout << "\n[Spotify] Получение Web Access Token...\n"; std::cout.flush();
            spotifyClient.AuthWithSpDc(spDc);
        } else {
            spotifyClient.SetAccessToken(savedToken);
            std::cout << "Проверка сохраненного токена Spotify...\n"; std::cout.flush();

            spotifyClient.ValidateToken([&, spDc](bool isValid) {
                if (isValid) {
                    console.SetState(ConsoleState::COMMAND_MODE);
                    std::cout << "\n[УСПЕХ] Синхронизация треков Spotify...\n> "; std::cout.flush();
                    initPlaylistAndStart(true);
                    vkSyncIndex = 0;
                    spotifyClient.FetchAllUserAudio(0, 50);
                } else {
                    std::cout << "\n[ВНИМАНИЕ] Токен Spotify устарел. Тихое обновление...\n"; std::cout.flush();
                    authManager.ClearSavedToken("Spotify");
                    spotifyClient.SetAccessToken("");
                    // Тихо запрашиваем новый токен, никаких окон!
                    spotifyClient.AuthWithSpDc(spDc);
                }
            });
        }
    };

    auto switchSource = [&](const std::string& newSource) {
        activeSource = newSource;
        Logger::Log(LogLevel::INFO, "Main: Switching audio source to " + newSource);

        // --- ЖЕСТКАЯ ОЧИСТКА СОСТОЯНИЯ ПРИ ПЕРЕКЛЮЧЕНИИ ---
        streamer.StopDownload();      // Обрываем текущее сетевое соединение
        audio.ClearBuffers(false, 0); // Вычищаем PCM-буферы и декодеры
        audio.Pause();                // Ставим движок на паузу
        isPlaybackStarted = false;    // Сбрасываем флаг старта
        playlist.Clear();             // Чистим очередь

        // --- СОХРАНЯЕМ ВЫБОР В НАСТРОЙКИ ---
        settings.setValue("General/source", QString::fromStdString(newSource));
        settings.sync();

        // Роутинг
        if (newSource == "VK") {
            currentProvider = &vkClient;
            startVkService();
        } else if (newSource == "Spotify") {
            currentProvider = &spotifyClient;
            startSpotifyService();
        } else if (newSource == "Offline") {
            currentProvider = nullptr;
            console.SetState(ConsoleState::COMMAND_MODE);
            initPlaylistAndStart(false);
        }
        console.SetCurrentProvider(currentProvider);
    };
    QObject::connect(&console, &ConsoleController::SourceChanged, &app, [&](const std::string& source) {
        switchSource(source);
    }, Qt::QueuedConnection);

    QString currentSource = settings.value("General/source", "VK").toString();
    switchSource(currentSource.toStdString());

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