#include "ApplicationCore.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QThreadPool>
#include <iostream>
#include <unordered_set>

#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "services/config/ConfigurationService.h"
#include "services/session/PlaybackSessionService.h"
#include "ui/console/core/ConsoleController.h"
#include "core/audio/playback/PlaybackController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

ApplicationCore::ApplicationCore(const QMap<QString, QString>& envVars, QObject* parent)
    : QObject(parent), m_envVars(envVars) {
    m_configService = std::make_unique<ConfigurationService>();
    m_sessionService = std::make_unique<PlaybackSessionService>();
}

ApplicationCore::~ApplicationCore() {
    if (m_audioPollTimer) {
        m_audioPollTimer->stop();
    }
    if (m_sessionService && m_audio && m_playlist && m_dbManager && m_configService) {
        m_sessionService->SaveSessionState(m_activeSource, *m_audio, *m_playlist, *m_dbManager, *m_configService);
    }
    QThreadPool::globalInstance()->waitForDone(2000);
}

bool ApplicationCore::Initialize() {
    m_configService->EnsureDefaultConfig();
    m_activeSource = m_configService->GetActiveSource();

    // Запрет прямого вывода логов в консоль, чтобы не ломать TUI
    Logger::SetConsoleOutputEnabled(false);

    // 1. Единый сетевой пул соединений QNetworkAccessManager
    m_networkManager = std::make_unique<QNetworkAccessManager>(this);

    // Подключение дискового HTTP-кэша (OPT-NET-04)
    auto diskCache = new QNetworkDiskCache(this);
    QString cachePath = PathManager::GetCacheDir() + "/http_cache";
    diskCache->setCacheDirectory(cachePath);
    int cacheSizeMb = m_configService->GetDiskCacheSizeMb();
    diskCache->setMaximumCacheSize(static_cast<qint64>(cacheSizeMb) * 1024 * 1024);
    m_networkManager->setCache(diskCache);
    Logger::Log(LogLevel::INFO, "HTTP Disk Cache initialized: dir=" + cachePath.toStdString() +
                " limit=" + std::to_string(cacheSizeMb) + " MB");

    // 2. Создание базовых сервисов
    m_dbManager = std::make_unique<DatabaseManager>();
    if (!m_dbManager->Init()) return false;

    m_audio = std::make_unique<MiniaudioEngine>();
    if (!m_audio->Init()) return false;

    m_playlist = std::make_unique<PlaylistManager>();
    m_downloader = std::make_unique<TrackDownloader>(this, m_networkManager.get());
    m_lyricsFetcher = std::make_unique<LyricsFetcher>(this, m_networkManager.get());
    m_streamer = std::make_unique<NetworkStreamer>(this, m_networkManager.get());

    // 3. DI в контроллеры
    m_playbackCtrl = std::make_unique<PlaybackController>(*m_audio, *m_playlist, *m_streamer);
    m_router = std::make_unique<SourceRouter>(m_envVars, m_networkManager.get(), this);

    m_console = std::make_unique<ConsoleController>(
        *m_audio, *m_playlist, *m_router->GetAuthManager(),
        *m_dbManager, *m_downloader, *m_lyricsFetcher,
        m_networkManager.get(), this
    );
    m_console->SetSourceRouter(m_router.get());

    m_sessionService->RestoreSessionState(*m_audio, *m_playlist, *m_playbackCtrl, *m_configService);
    WireConnections();

    m_audioPollTimer = new QTimer(this);
    connect(m_audioPollTimer, &QTimer::timeout, this, [this]() {
        if (m_audio) {
            m_audio->PollEvents();

            size_t netBuf = m_audio->GetNetworkBufferSize();
            if (netBuf <= 512 * 1024) {
                m_streamer->ResumeDownload();
            } else if (netBuf >= 2 * 1024 * 1024) {
                m_streamer->PauseDownload();
            }
        }
    });
    m_audioPollTimer->start(20);

    return true;
}

void ApplicationCore::Start() {
    m_router->EnsureAllProvidersInitialized();
    m_router->SwitchSource(m_activeSource);
    m_console->Start();
}

void ApplicationCore::WireConnections() {
    // Сеть -> Аудио
    connect(m_streamer.get(), &NetworkStreamer::DataReceived, this, [this](const QByteArray& data) {
        m_audio->PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
        if (m_audio->GetNetworkBufferSize() >= 2 * 1024 * 1024) {
            m_streamer->PauseDownload();
        }
    });
    connect(m_streamer.get(), &NetworkStreamer::ExactSeekOffset, this, [this](double skipSeconds) {
        m_audio->SetNetworkSkipSeconds(skipSeconds);
    });
    connect(m_streamer.get(), &NetworkStreamer::DownloadFinished, this, [this]() {
        m_audio->SetNetworkStreamFinished();
    });
    connect(m_streamer.get(), &NetworkStreamer::DownloadError, this, [this](const std::string& err) {
        m_audio->SetNetworkStreamFinished();
        m_console->SetStatusMessage("[Ошибка] Ошибка стриминга: " + err);
    });

    // Аудио -> Воспроизведение
    m_audio->OnNetworkSeekRequested = [this](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, targetSeconds]() {
            m_streamer->SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };
    m_audio->OnTrackFinished = [this]() { m_playbackCtrl->HandleTrackFinished(); };
    m_audio->OnTrackNearEnd = [this]() { m_playbackCtrl->HandleTrackNearEnd(); };
    m_audio->OnPlaybackError = [this](const std::string& err) {
        Logger::Log(LogLevel::ERROR, "Playback failed: " + err + ". Skipping to next track...");
        m_console->SetStatusMessage("[Ошибка] Ошибка воспроизведения: " + err);
        m_playlist->Next();
    };

    // Плейлист -> Воспроизведение
    m_playlist->OnTrackRequested = [this](Track track) { m_playbackCtrl->AttemptPlay(track); };

    // UI команды
    connect(m_console.get(), &ConsoleController::QuitRequested, this, []() {
        Logger::Log(LogLevel::INFO, ">>> ApplicationCore: QuitRequested received from ConsoleController! <<<");
    });
    connect(m_console.get(), &ConsoleController::QuitRequested, QCoreApplication::instance(), &QCoreApplication::quit);
    connect(m_console.get(), &ConsoleController::OfflineModeRequested, this, [this]() { InitPlaylistAndStart(false); }, Qt::QueuedConnection);
    connect(m_console.get(), &ConsoleController::SourceChanged, m_router.get(), [this](const std::string& source) {
        m_router->SwitchSource(source);
    }, Qt::QueuedConnection);
    connect(m_console.get(), &ConsoleController::LogoutRequested, this, &ApplicationCore::HandleLogout, Qt::QueuedConnection);

    m_console->OnGaplessModeChanged = [this](bool isCrossfade) {
        m_configService->SetCrossfadeEnabled(isCrossfade);
        m_playbackCtrl->SetCrossfadeEnabled(isCrossfade);
        Logger::Log(LogLevel::INFO, std::string("Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    // Роутер событий
    connect(m_router.get(), &SourceRouter::SourceChanged, this, [this](const std::string& newSource) {
        if (!m_activeSource.empty() && m_playlist->HasTracks()) {
            m_sessionService->SaveSessionState(m_activeSource, *m_audio, *m_playlist, *m_dbManager, *m_configService);
        }
        m_activeSource = newSource;
        m_playbackCtrl->ClearState();
        m_isPlaybackStarted = false;
        m_playlist->Clear();
        m_configService->SetActiveSource(newSource);
        m_console->SetCurrentProvider(m_router->GetCurrentProvider());
        m_playbackCtrl->SetCurrentProvider(m_router->GetCurrentProvider());
    });

    connect(m_router.get(), &SourceRouter::AuthUiStateChanged, this, [this](bool isWaiting) {
        if (isWaiting) {
            m_playbackCtrl->CancelPlaybackAndRetries();
        }
        m_console->SetState(isWaiting ? ConsoleState::WAITING_TOKEN_URL : ConsoleState::COMMAND_MODE);
    });

    connect(m_router.get(), &SourceRouter::ProviderReady, this, [this](bool isOnline) {
        m_vkSyncIndex = 0;
        InitPlaylistAndStart(isOnline);
    });

    // TASK-19: Вывод статусов авторизации и сервисов в TUI
    connect(m_router.get(), &SourceRouter::StatusMessageRequested, this, [this](const std::string& msg) {
        m_console->SetStatusMessage(msg);
    });

    // TASK-20: Provider Registry — получение обновлений треков через события роутера
    connect(m_router.get(), &SourceRouter::AudioFetched, this, &ApplicationCore::OnAudioFetched);
    connect(m_router.get(), &SourceRouter::FinishedFetching, this, &ApplicationCore::OnFinishedFetching);

    m_playbackCtrl->SetProviderResolver([this](const std::string& source) {
        return m_router->GetProvider(source);
    });
}

void ApplicationCore::InitPlaylistAndStart(bool isOnline) {
    if (m_isPlaybackStarted) return;

    auto sessionOpt = m_sessionService->LoadSessionForSource(m_activeSource, *m_dbManager, *m_configService);
    m_sessionService->PopulatePlaylistFromStorage(m_activeSource, isOnline, *m_dbManager, *m_playlist, *m_configService);

    if (m_playlist->HasTracks()) {
        m_isPlaybackStarted = true;
        int savePosMode = m_configService->GetSavePositionMode();
        if (sessionOpt.has_value()) {
            m_sessionService->ApplySessionToPlayback(sessionOpt.value(), savePosMode, *m_playlist, *m_playbackCtrl);
        } else {
            m_playbackCtrl->SetSavedPosition(0.0);
            m_playlist->OnTrackRequested(m_playlist->GetCurrentTrack());
        }
    } else {
        Logger::Log(LogLevel::WARNING, "[Оффлайн] Нет скачанных треков. Плеер пуст.");
    }
}

void ApplicationCore::OnAudioFetched(const std::vector<Track>& tracks) {
    auto allTracks = m_playlist->GetAllTracks();

    std::unordered_set<std::string> existingIds;
    existingIds.reserve(allTracks.size());
    for (const auto& c : allTracks) {
        existingIds.insert(c.id);
    }

    for (const auto& track : tracks) {
        if (existingIds.find(track.id) == existingIds.end()) {
            m_playlist->InsertTrack(m_vkSyncIndex, track);
            existingIds.insert(track.id);
        }
        m_vkSyncIndex++;
    }

    m_dbManager->SaveTracks(tracks);
    if (!m_isPlaybackStarted) InitPlaylistAndStart(true);
}

void ApplicationCore::OnFinishedFetching() {
    Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
    m_dbManager->SaveQueue(m_playlist->GetAllTracks(), m_activeSource, false);
    m_dbManager->SaveQueue(m_playlist->GetQueueTracks(), m_activeSource, m_playlist->IsShuffle());
    m_dbManager->ExportQueueToTxt(m_playlist->GetQueueTracks(), "playlist.txt", m_playlist->IsShuffle());
}

void ApplicationCore::HandleLogout(const std::string& service) {
    Logger::Log(LogLevel::INFO, "ApplicationCore: Handling logout for service: " + service);

    std::string lowerSvc = service;
    for (char& c : lowerSvc) c = std::tolower(c);

    std::string canonicalSvc = "";
    if (lowerSvc == "vk") canonicalSvc = "VK";
    else if (lowerSvc == "spotify") canonicalSvc = "Spotify";
    else if (lowerSvc == "sc" || lowerSvc == "soundcloud") canonicalSvc = "SoundCloud";
    else if (lowerSvc == "yandex") canonicalSvc = "Yandex";
    else if (lowerSvc == "youtube" || lowerSvc == "yt") canonicalSvc = "YouTube";
    else if (lowerSvc == "all") canonicalSvc = "all";
    else canonicalSvc = service;

    std::vector<std::string> servicesToClear;
    if (canonicalSvc == "all") {
        servicesToClear = {"VK", "Spotify", "SoundCloud", "Yandex", "YouTube"};
    } else {
        servicesToClear = {canonicalSvc};
    }

    for (const auto& svc : servicesToClear) {
        m_router->Logout(svc);
        m_dbManager->ClearTracksForSource(svc);
        m_dbManager->ClearSourceSession(svc);
    }

    bool activeAffected = false;
    if (canonicalSvc == "all") {
        activeAffected = true;
    } else {
        if (QString::compare(QString::fromStdString(m_activeSource), QString::fromStdString(canonicalSvc), Qt::CaseInsensitive) == 0) {
            activeAffected = true;
        }
    }

    if (!activeAffected) {
        m_console->SetStatusMessage("[Выход] Токен, кэш и треки в БД для " + canonicalSvc + " удалены.");
        return;
    }

    m_playbackCtrl->ClearState();
    m_audio->Pause();
    m_isPlaybackStarted = false;
    m_playlist->Clear();

    QFile::remove(PathManager::GetPlaylistExportPath("playlist.txt"));

    m_router->FindNextAuthorizedSource(canonicalSvc, [this](const std::string& nextSource) {
        if (nextSource != "Offline") {
            m_console->SetStatusMessage("[Выход] Переключение на авторизованный сервис: " + nextSource);
            m_router->SwitchSource(nextSource);
        } else {
            m_console->SetStatusMessage("[Выход] Авторизованных аккаунтов не найдено. Переключено в Оффлайн режим.");
            m_router->SwitchSource("Offline");
        }
    });
}