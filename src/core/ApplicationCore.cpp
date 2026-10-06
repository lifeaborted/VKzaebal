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
#include "ui/IUiController.h"
#include "ui/UiFactory.h"
#include "core/audio/playback/PlaybackController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

ApplicationCore::ApplicationCore(const QMap<QString, QString>& envVars, UiMode uiMode, QObject* parent)
    : QObject(parent), m_envVars(envVars), m_uiMode(uiMode) {
    m_configService = std::make_unique<ConfigurationService>();
    m_sessionService = std::make_unique<PlaybackSessionService>();
}

ApplicationCore::~ApplicationCore() {
    if (m_audioPollTimer) {
        m_audioPollTimer->stop();
    }
    if (m_ui) {
        m_ui->Stop();
    }
    if (m_streamer) {
        m_streamer->StopDownload();
    }
    if (m_audio) {
        m_audio->Pause();
    }
    if (m_sessionService && m_audio && m_playlist && m_dbManager && m_configService) {
        m_sessionService->SaveSessionState(m_activeSource, *m_audio, *m_playlist, *m_dbManager, *m_configService);
    }
    QThreadPool::globalInstance()->waitForDone(2000);
}

IAudioEngine& ApplicationCore::GetAudio() const { return *m_audio; }
PlaylistManager& ApplicationCore::GetPlaylist() const { return *m_playlist; }
SourceRouter& ApplicationCore::GetRouter() const { return *m_router; }
OAuthManager* ApplicationCore::GetAuthManager() const { return m_router ? m_router->GetAuthManager() : nullptr; }
DatabaseManager& ApplicationCore::GetDbManager() const { return *m_dbManager; }
TrackDownloader& ApplicationCore::GetDownloader() const { return *m_downloader; }
LyricsFetcher& ApplicationCore::GetLyricsFetcher() const { return *m_lyricsFetcher; }
PlaybackController& ApplicationCore::GetPlaybackCtrl() const { return *m_playbackCtrl; }
ConfigurationService* ApplicationCore::GetConfigService() const { return m_configService.get(); }
QNetworkAccessManager* ApplicationCore::GetNetworkManager() const { return m_networkManager.get(); }
PlaybackSessionService* ApplicationCore::GetSessionService() const { return m_sessionService.get(); }
NetworkStreamer& ApplicationCore::GetStreamer() const { return *m_streamer; }

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

    // 4. Создание или внедрение UI через UiFactory / Dependency Injection
    if (m_uiFactory) {
        m_ui = m_uiFactory(*this);
    } else if (!m_ui) {
        m_ui = UiFactory::Create(m_uiMode, *this);
    }

    if (!m_ui) {
        Logger::Log(LogLevel::ERROR, "ApplicationCore: Failed to create UI controller");
        return false;
    }

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
    if (m_ui) {
        m_ui->Start();
    }
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
        std::string msg = "[Ошибка] Ошибка стриминга: " + err;
        if (m_ui) m_ui->SetStatusMessage(msg);
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
        std::string msg = "[Ошибка] Ошибка воспроизведения: " + err;
        if (m_ui) m_ui->SetStatusMessage(msg);
        m_playlist->Next();
    };

    // Плейлист -> Воспроизведение
    m_playlist->OnTrackRequested = [this](const Track& track) {
        m_playbackCtrl->AttemptPlay(track);
        if (m_ui) m_ui->OnTrackChanged(track);
    };

    // UI команды
    if (m_ui) {
        connect(m_ui.get(), &IUiController::QuitRequested, this, []() {
            Logger::Log(LogLevel::INFO, ">>> ApplicationCore: QuitRequested received! <<<");
            QCoreApplication::quit();
        });

        connect(m_ui.get(), &IUiController::OfflineModeRequested, this, [this]() {
            InitPlaylistAndStart(false);
        }, Qt::QueuedConnection);

        connect(m_ui.get(), &IUiController::SourceChanged, m_router.get(), [this](const std::string& source) {
            if (source == m_activeSource && !m_activeSource.empty()) return;
            m_router->SwitchSource(source);
        }, Qt::QueuedConnection);

        connect(m_ui.get(), &IUiController::LogoutRequested, this, &ApplicationCore::HandleLogout, Qt::QueuedConnection);

        m_ui->OnGaplessModeChanged = [this](bool isCrossfade) {
            m_configService->SetCrossfadeEnabled(isCrossfade);
            m_playbackCtrl->SetCrossfadeEnabled(isCrossfade);
            Logger::Log(LogLevel::INFO, std::string("Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
        };
    }

    // Роутер событий
    connect(m_router.get(), &SourceRouter::SourceChanged, this, [this](const std::string& newSource) {
        if (!m_activeSource.empty() && m_playlist->HasTracks()) {
            m_sessionService->SaveSessionState(m_activeSource, *m_audio, *m_playlist, *m_dbManager, *m_configService);
        }
        m_activeSource = newSource;
        m_configService->SetActiveSource(newSource);
        if (m_ui) m_ui->SetCurrentProvider(m_router->GetCurrentProvider());
        m_playbackCtrl->SetCurrentProvider(m_router->GetCurrentProvider());

        bool isAudioActive = m_audio->IsPlaying() || m_isPlaybackStarted;
        if (!isAudioActive) {
            m_playbackCtrl->ClearState();
            m_isPlaybackStarted = false;
            m_playlist->Clear();
        } else {
            m_playlist->ClearKeepActive();
        }
    });

    connect(m_router.get(), &SourceRouter::AuthUiStateChanged, this, [this](bool isWaiting) {
        if (isWaiting) {
            m_playbackCtrl->CancelPlaybackAndRetries();
        }
        if (m_ui) {
            m_ui->SetWaitingAuth(isWaiting);
        }
    });

    connect(m_router.get(), &SourceRouter::ProviderReady, this, [this](bool isOnline) {
        m_syncIndex = 0;
        if (!m_isPlaybackStarted) {
            InitPlaylistAndStart(isOnline);
        } else {
            m_sessionService->PopulatePlaylistFromStorage(m_activeSource, isOnline, *m_dbManager, *m_playlist, *m_configService);
            m_playlist->AlignWithActiveTrack();
        }
        m_syncExistingIds.clear();
        for (const auto& t : m_playlist->GetAllTracks()) {
            m_syncExistingIds.insert(t.id);
        }
    });

    // Вывод статусов авторизации и сервисов в UI
    connect(m_router.get(), &SourceRouter::StatusMessageRequested, this, [this](const std::string& msg) {
        if (m_ui) m_ui->SetStatusMessage(msg);
    });

    // Provider Registry — получение обновлений треков через события роутера
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
    for (const auto& track : tracks) {
        if (m_syncExistingIds.find(track.id) == m_syncExistingIds.end()) {
            m_playlist->InsertTrack(m_syncIndex, track);
            m_syncExistingIds.insert(track.id);
        } else {
            int existingPos = m_playlist->FindTrackIndexById(track.id);
            if (existingPos >= 0 && existingPos != m_syncIndex) {
                m_playlist->MoveTrack(existingPos, m_syncIndex);
            }
        }
        m_syncIndex++;
    }

    m_dbManager->SaveTracks(tracks);
    if (!m_isPlaybackStarted) {
        InitPlaylistAndStart(true);
    }
    m_playlist->AlignWithActiveTrack();
    if (m_ui) {
        m_ui->OnAudioFetched(tracks);
    }
}

void ApplicationCore::OnFinishedFetching() {
    Logger::Log(LogLevel::INFO, "=== ФОНОВАЯ СИНХРОНИЗАЦИЯ ЗАВЕРШЕНА ===");
    m_syncExistingIds.clear();
    m_playlist->AlignWithActiveTrack();
    m_dbManager->SaveQueue(m_playlist->GetAllTracks(), m_activeSource, false);
    if (m_playlist->IsShuffle()) {
        m_dbManager->SaveQueue(m_playlist->GetQueueTracks(), m_activeSource, true);
    }
    m_dbManager->ExportQueueToTxt(m_playlist->GetQueueTracks(), "playlist.txt", m_playlist->IsShuffle());
    if (m_ui) {
        m_ui->OnFinishedFetching();
    }
}

void ApplicationCore::HandleLogout(const std::string& service) {
    Logger::Log(LogLevel::INFO, "ApplicationCore: Handling logout for service: " + service);

    auto setStatus = [this](const std::string& msg) {
        if (m_ui) m_ui->SetStatusMessage(msg);
    };

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
        setStatus("[Выход] Токен, кэш и треки в БД для " + canonicalSvc + " удалены.");
        return;
    }

    m_playbackCtrl->ClearState();
    m_audio->Pause();
    m_isPlaybackStarted = false;
    m_playlist->Clear();

    QFile::remove(PathManager::GetPlaylistExportPath("playlist.txt"));

    m_router->FindNextAuthorizedSource(canonicalSvc, [this, setStatus](const std::string& nextSource) {
        if (nextSource != "Offline") {
            setStatus("[Выход] Переключение на авторизованный сервис: " + nextSource);
            m_router->SwitchSource(nextSource);
        } else {
            setStatus("[Выход] Авторизованных аккаунтов не найдено. Переключено в Оффлайн режим.");
            m_router->SwitchSource("Offline");
        }
    });
}