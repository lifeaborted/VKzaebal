#include <atomic>
#include <iostream>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <string>
#include <QtWebView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QTextStream>
#include <QMap>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/audio/IAudioEngine.h"
#include "core/audio/bass/BassEngine.h"
#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/api/Client.h"
#include "core/vk/auth/Manager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "ui/console/ConsoleController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"


// Функция для парсинга .env файла
QMap<QString, QString> LoadEnvFile(const QString& filePath) {
    QMap<QString, QString> env;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int idx = line.indexOf('=');
            if (idx != -1) {
                QString key = line.left(idx).trimmed();
                QString value = line.mid(idx + 1).trimmed();
                if (value.startsWith('"') && value.endsWith('"')) {
                    value = value.mid(1, value.length() - 2);
                }
                env[key] = value;
            }
        }
    } else {
        Logger::Log(LogLevel::WARNING, "Main: .env file not found at " + filePath.toStdString());
    }
    return env;
}

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
    QMap<QString, QString> envVars = LoadEnvFile(".env");
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");
    Logger::Log(LogLevel::INFO, "DB Path: " + PathManager::GetDbPath().toStdString());
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);

    if (!settings.contains("Audio/CrossfadeDurationMs")) {
        settings.setValue("Audio/CrossfadeDurationMs", 3000);
        settings.sync();
    }

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;

    //BassEngine audio;
    MiniaudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    Client vkClient;
    Manager authManager;
    TrackDownloader downloader;
    LyricsFetcher lyricsFetcher;
    NetworkStreamer streamer;

    bool crossfadeEnabled = settings.value("Audio/CrossfadePlayback", false).toBool();
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();
    bool autoPlay = settings.value("Session/AutoPlay", false).toBool();

    //playlist.SetShuffle(isShuffle); // Применяем шафл к плейлисту
    Track preloadedTrack;
    std::string cachedNextUrl = "";

    float savedVolume = settings.value("Session/Volume", 1.0f).toFloat();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();
    double savedPosition = settings.value("Session/Position", 0.0).toDouble();

    audio.SetVolume(savedVolume);

    std::vector<Track> cachedTracks = dbManager.LoadTracks();
    bool isPlaybackStarted = false;
    int vkSyncIndex = 0;

    // Связываем скачивание из сети с демуксером в движке
    QObject::connect(&streamer, &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        // Переводим QByteArray в uint8_t байты  и пушим в декодер FDK-AAC
        audio.PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    audio.OnNetworkSeekRequested = [&](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&streamer, targetSeconds]() {
            streamer.SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };

    ConsoleController console(audio, playlist, authManager, dbManager, vkClient, downloader, lyricsFetcher);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    // Умная инициализация плейлиста
    auto initPlaylistAndStart = [&](bool isOnline) {
        if (isPlaybackStarted) return;

        if (!playlist.HasTracks()) {
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
                playlist.SetShuffle(true);
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
                playlist.InsertTrack(vkSyncIndex, track);
                hasNewTracks = true;
            }
            vkSyncIndex++;
        }

        dbManager.SaveTracks(tracks);

                if (!isPlaybackStarted) {
                    initPlaylistAndStart(true);
                    dbManager.SaveQueue(playlist.GetAllTracks(), false);
                    dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
                    dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());
                } else if (hasNewTracks) {
                    dbManager.SaveQueue(playlist.GetAllTracks(), false);
                    dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
                    dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());
                }
            });

    QObject::connect(&vkClient, &Client::FinishedFetching, [&]() {
        Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");

        dbManager.SaveQueue(playlist.GetAllTracks(), false);
        dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
        dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());
    });

    QQmlApplicationEngine* authEngine = nullptr;
    QString currentAuthService = "";

    // УНИВЕРСАЛЬНЫЙ МЕТОД АВТОРИЗАЦИИ
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
            authEngine->load(QUrl(QStringLiteral("qrc:/core/vk/auth/auth.qml")));

            if (authEngine->rootObjects().isEmpty()) {
                Logger::Log(LogLevel::ERROR, "Main: Failed to load auth.qml!");
            }
        }
    };

// 1. Коннект для неявного токена (ВКонтакте)
    QObject::connect(&authManager, &Manager::TokenReceived, [&](const std::string& token) {
        if (authEngine) {
            authEngine->deleteLater();
            authEngine = nullptr;
            Logger::Log(LogLevel::INFO, "Main: QML Engine scheduled for destruction, memory freed.");
        }

        // Сохраняем токен именно для того сервиса, который открыл окно
        authManager.SaveToken(token, currentAuthService);
        console.SetState(ConsoleState::COMMAND_MODE);

        std::cout << "\n[УСПЕХ] Авторизация " << currentAuthService.toStdString() << " пройдена!\n> ";
        std::cout.flush();

        // Роутинг действий после успешной авторизации
        if (currentAuthService == "VK") {
            vkClient.SetAccessToken(token);
            initPlaylistAndStart(true);
            vkClient.FetchAllUserAudio(0, 200);
        }
    });

    // Локальный менеджер сети для запроса токенов
    QNetworkAccessManager* spotifyAuthNetManager = new QNetworkAccessManager(&app);

    // 2. Коннект для кода авторизации (Spotify)
    QObject::connect(&authManager, &Manager::AuthCodeReceived, &app, [&](const std::string& code) {
        if (authEngine) {
            authEngine->deleteLater();
            authEngine = nullptr;
        }

        std::cout << "\n[Spotify] Код получен. Отправка POST-запроса на обмен токена...\n";
        std::cout.flush();

        QString clientId = envVars.value("SPOTIFY_CLIENT_ID", "");
        QString clientSecret = envVars.value("SPOTIFY_CLIENT_SECRET", "");

        if (clientId.isEmpty() || clientSecret.isEmpty()) {
            std::cout << "\n[ОШИБКА] Ключи Spotify не найдены в .env файле!\n> ";
            std::cout.flush();
            console.SetState(ConsoleState::COMMAND_MODE);
            return;
        }

        QUrl url("https://accounts.spotify.com/api/token");
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        // Создаем заголовок Basic Auth по стандарту OAuth 2.0
        QByteArray auth = (clientId + ":" + clientSecret).toUtf8().toBase64();
        request.setRawHeader("Authorization", "Basic " + auth);

        QString decodedCode = QUrl::fromPercentEncoding(QString::fromStdString(code).toUtf8());

        QByteArray body;
        body.append("grant_type=authorization_code");
        body.append("&code=" + QUrl::toPercentEncoding(decodedCode));
        body.append("&redirect_uri=" + QUrl::toPercentEncoding("http://127.0.0.1:8080/callback"));

        QNetworkReply* reply = spotifyAuthNetManager->post(request, body);

        QObject::connect(reply, &QNetworkReply::finished, [reply, &authManager, &console]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
                std::string token = json.object()["access_token"].toString().toStdString();

                authManager.SaveToken(token, "Spotify");
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[УСПЕХ] Авторизация Spotify пройдена!\n> ";
                std::cout.flush();
            } else {
                std::cout << "\n[ОШИБКА] Не удалось получить токен: " << reply->errorString().toStdString() << "\n> ";
                std::cout.flush();
                console.SetState(ConsoleState::COMMAND_MODE);
            }
            reply->deleteLater();
        });
    });

    QObject::connect(&vkClient, &Client::TokenExpired, [&]() {
        if (console.GetState() == ConsoleState::WAITING_TOKEN_URL) return;
        Logger::Log(LogLevel::WARNING, "Main: Token VK expired.");
        std::cout << "\n[ВНИМАНИЕ] Токен ВК устарел (Или сменился IP из-за VPN).\n";

        authManager.ClearSavedToken("VK");
        vkClient.SetAccessToken("");

        QString vkUrl = "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131";
        startAuthFlow("VK", vkUrl);
    });

    // === РОУТЕР ИСТОЧНИКОВ ===

    // 1. ВКонтакте
    auto startVkService = [&]() {
        std::string savedToken = authManager.GetSavedToken("VK");
        if (savedToken.empty()) {
            QString vkUrl = "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131";
            startAuthFlow("VK", vkUrl);
        } else {
            vkClient.SetAccessToken(savedToken);
            std::cout << "Проверка сохраненного токена ВК...\n";
            std::cout.flush();

            vkClient.ValidateToken([&, startAuthFlow, initPlaylistAndStart](bool isValid) {
                if (isValid) {
                    console.SetState(ConsoleState::COMMAND_MODE);
                    std::cout << "\n[УСПЕХ] Синхронизация свежих треков ВК...\n> ";
                    std::cout.flush();
                    initPlaylistAndStart(true);
                    vkSyncIndex = 0;
                    vkClient.FetchAllUserAudio(0, 200);
                } else {
                    std::cout << "\n[ВНИМАНИЕ] Нет сети или токен ВК недействителен.\n";
                    std::cout.flush();
                    authManager.ClearSavedToken("VK");
                    vkClient.SetAccessToken("");
                    QString vkUrl = "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131";
                    startAuthFlow("VK", vkUrl);
                }
            });
        }
    };

    // 2. Spotify
    auto startSpotifyService = [&]() {
        std::string savedToken = authManager.GetSavedToken("Spotify");
        if (savedToken.empty()) {
            QString clientId = envVars.value("SPOTIFY_CLIENT_ID", "");

            if (clientId.isEmpty()) {
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[ОШИБКА] SPOTIFY_CLIENT_ID не задан в .env файле!\n> ";
                std::cout.flush();
                return;
            }

            QString spotUrl = "https://accounts.spotify.com/authorize?client_id=" + clientId + "&response_type=code&redirect_uri=http://127.0.0.1:8080/callback&scope=user-library-read%20user-read-playback-state%20playlist-read-private&show_dialog=true";

            startAuthFlow("Spotify", spotUrl);
        } else {
            console.SetState(ConsoleState::COMMAND_MODE);
            std::cout << "\n[Spotify] Токен найден! Подключение Spotify Web API (в разработке)...\n> ";
            std::cout.flush();
        }
    };

    // 3. Переключатель
    auto switchSource = [&](const std::string& newSource) {
        Logger::Log(LogLevel::INFO, "Main: Switching audio source to " + newSource);

        audio.Pause();
        isPlaybackStarted = false;
        playlist.Clear();

        if (newSource == "VK") {
            startVkService();
        } else if (newSource == "Spotify") {
            startSpotifyService();
        } else if (newSource == "Offline") {
            console.SetState(ConsoleState::COMMAND_MODE);
            initPlaylistAndStart(false);
        }
    };

    QObject::connect(&console, &ConsoleController::SourceChanged, &app, [&](const std::string& source) {
        switchSource(source);
    }, Qt::QueuedConnection);

    // === ПЕРВЫЙ ЗАПУСК ===
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