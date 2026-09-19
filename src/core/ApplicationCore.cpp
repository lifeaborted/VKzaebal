#include "ApplicationCore.h"
#include <QCoreApplication>
#include <QSettings>
#include <QFile>
#include <QTimer>
#include <iostream>
#include <cmath>
#include <unordered_set>

#include "core/audio/miniaudio/MiniaudioEngine.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "ui/console/core/ConsoleController.h"
#include "core/audio/playback/PlaybackController.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

ApplicationCore::ApplicationCore(const QMap<QString, QString>& envVars, QObject* parent)
    : QObject(parent), m_envVars(envVars) {
}

ApplicationCore::~ApplicationCore() {
    if (m_audioPollTimer) {
        m_audioPollTimer->stop();
    }
    SaveSession();
}

bool ApplicationCore::Initialize() {
    EnsureDefaultConfig();

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_activeSource = settings.value("General/source", "VK").toString().toStdString();

    // Запрет прямого вывода логов в консоль, чтобы не ломать TUI
    Logger::SetConsoleOutputEnabled(false);

    // 1. Создание базовых сервисов
    m_dbManager = std::make_unique<DatabaseManager>();
    if (!m_dbManager->Init()) return false;

    m_audio = std::make_unique<MiniaudioEngine>();
    if (!m_audio->Init()) return false;

    m_playlist = std::make_unique<PlaylistManager>();
    m_downloader = std::make_unique<TrackDownloader>();
    m_lyricsFetcher = std::make_unique<LyricsFetcher>();
    m_streamer = std::make_unique<NetworkStreamer>();

    // 2. Внедрение зависимостей (Dependency Injection) в контроллеры
    m_playbackCtrl = std::make_unique<PlaybackController>(*m_audio, *m_playlist, *m_streamer);
    m_router = std::make_unique<SourceRouter>(m_envVars);
    
    m_console = std::make_unique<ConsoleController>(
        *m_audio, *m_playlist, *m_router->GetAuthManager(),
        *m_dbManager, *m_downloader, *m_lyricsFetcher
    );

    RestoreSession();
    WireConnections();

    m_audioPollTimer = new QTimer(this);
    connect(m_audioPollTimer, &QTimer::timeout, this, [this]() {
        if (m_audio) {
            m_audio->PollEvents();
        }
    });
    m_audioPollTimer->start(20);

    return true;
}

void ApplicationCore::EnsureDefaultConfig() {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    if (!settings.contains("Session/Volume")) {
        settings.setValue("Audio/CrossfadeDurationMs", 3000);
        settings.setValue("Audio/CrossfadePlayback", false);
        settings.setValue("Session/Volume", 1.0f);
        settings.setValue("Session/Shuffle", false);
        settings.setValue("Session/AutoPlay", false);
        settings.setValue("Session/Repeat", 1);
        settings.setValue("Playback/SavePosition", 2);
        settings.setValue("General/source", "VK");
        settings.setValue("Ui/ShowVisualizer", true);
        settings.setValue("Downloads/Path", "");
        settings.sync();
    } else {
        if (!settings.contains("Downloads/Path")) {
            settings.setValue("Downloads/Path", "");
        }
        if (!settings.contains("Playback/SavePosition")) {
            settings.setValue("Playback/SavePosition", 2);
        }
        if (settings.contains("Session/Position")) {
            settings.remove("Session/Position");
        }
        if (settings.contains("Session/CurrentTrackIndex")) {
            settings.remove("Session/CurrentTrackIndex");
        }
        settings.sync();
    }

    QString ultimatePath = PathManager::GetUltimateConfigPath();
    if (!QFile::exists(ultimatePath)) {
        QSettings defaultUltimate(ultimatePath, QSettings::IniFormat);
        defaultUltimate.setValue("Visualizer/Height", 10);
        defaultUltimate.setValue("Visualizer/Width", 0);
        defaultUltimate.setValue("Visualizer/BarWidth", 2);
        defaultUltimate.setValue("Visualizer/BarSpacing", 1);
        defaultUltimate.setValue("Visualizer/BlockSpacing", 1);
        defaultUltimate.setValue("Visualizer/DrawBorders", true);
        defaultUltimate.setValue("Visualizer/Layout", "Visualizer,ProgressBar,TrackInfo");
        defaultUltimate.setValue("Visualizer/PaddingLeft", 2);
        defaultUltimate.setValue("Visualizer/Color", "gradient");
        defaultUltimate.setValue("Visualizer/GradientColors", "#32FF96,#F0B432,#FF5050");
        defaultUltimate.setValue("Background/Enabled", false);
        defaultUltimate.setValue("Background/Color", "gradient");
        defaultUltimate.setValue("Background/GradientColors", "#1E1E1E,#000000");
        defaultUltimate.setValue("Visualizer/Framerate", 30);
        defaultUltimate.setValue("Visualizer/Smoothing", 0.5);
        defaultUltimate.sync();
    }
}

void ApplicationCore::RestoreSession() {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    
    m_playlist->SetRepeatMode(settings.value("Session/Repeat", 1).toInt());
    m_audio->SetVolume(settings.value("Session/Volume", 1.0f).toFloat());
    
    m_playbackCtrl->SetCrossfadeEnabled(settings.value("Audio/CrossfadePlayback", false).toBool());

    m_playbackCtrl->SetStartPaused(!settings.value("Session/AutoPlay", false).toBool());
}

void ApplicationCore::SaveSession() {
    Logger::Log(LogLevel::INFO, "ApplicationCore: Saving session state...");
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/Volume", m_audio->GetVolume());
    settings.setValue("Session/Shuffle", m_playlist->IsShuffle());
    settings.setValue("Session/Repeat", m_playlist->GetRepeatMode());
    settings.remove("Session/CurrentTrackIndex");
    settings.remove("Session/Position");
    settings.sync();

    if (!m_activeSource.empty() && m_playlist->HasTracks()) {
        std::string currentTrackId = m_playlist->GetCurrentTrack().id;
        int currentIndex = m_playlist->GetCurrentAbsoluteIndex();
        double currentPos = m_audio->GetPositionSeconds();
        m_dbManager->SaveSourceSession(m_activeSource, currentTrackId, currentIndex, currentPos);
    }
}

void ApplicationCore::Start() {
    m_router->SwitchSource(m_activeSource);
    m_console->Start();
}

void ApplicationCore::WireConnections() {
    // Сеть -> Аудио
    connect(m_streamer.get(), &NetworkStreamer::DataReceived, [&](const QByteArray& data) {
        m_audio->PushNetworkData(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });
    connect(m_streamer.get(), &NetworkStreamer::ExactSeekOffset, [&](double skipSeconds) {
        m_audio->SetNetworkSkipSeconds(skipSeconds);
    });

    connect(m_streamer.get(), &NetworkStreamer::DownloadFinished, [&]() {
        m_audio->SetNetworkStreamFinished();
    });
    connect(m_streamer.get(), &NetworkStreamer::DownloadError, [&](const std::string& err) {
        m_audio->SetNetworkStreamFinished();
        m_console->SetStatusMessage("[Ошибка] Ошибка стриминга: " + err);
    });

    // Аудио -> Воспроизведение
    m_audio->OnNetworkSeekRequested = [&](double targetSeconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [&, targetSeconds]() {
            m_streamer->SeekTo(targetSeconds);
        }, Qt::QueuedConnection);
    };
    m_audio->OnTrackFinished = [&]() { m_playbackCtrl->HandleTrackFinished(); };
    m_audio->OnTrackNearEnd = [&]() { m_playbackCtrl->HandleTrackNearEnd(); };
    m_audio->OnPlaybackError = [&](const std::string& err) {
        Logger::Log(LogLevel::ERROR, "Playback failed: " + err + ". Skipping to next track...");
        m_console->SetStatusMessage("[Ошибка] Ошибка воспроизведения: " + err);
        m_playlist->Next();
    };

    // Плейлист -> Воспроизведение
    m_playlist->OnTrackRequested = [&](Track track) { m_playbackCtrl->AttemptPlay(track); };

    // UI команды
    connect(m_console.get(), &ConsoleController::QuitRequested, QCoreApplication::instance(), &QCoreApplication::quit);
    connect(m_console.get(), &ConsoleController::OfflineModeRequested, this, [&]() { InitPlaylistAndStart(false); }, Qt::QueuedConnection);
    connect(m_console.get(), &ConsoleController::SourceChanged, m_router.get(), [&](const std::string& source) {
        m_router->SwitchSource(source);
    }, Qt::QueuedConnection);
    connect(m_console.get(), &ConsoleController::LogoutRequested, this, &ApplicationCore::HandleLogout, Qt::QueuedConnection);

    m_console->OnGaplessModeChanged = [&](bool isCrossfade) {
        QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Audio/CrossfadePlayback", isCrossfade);
        m_playbackCtrl->SetCrossfadeEnabled(isCrossfade);
        Logger::Log(LogLevel::INFO, std::string("Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    // Роутер событий
    connect(m_router.get(), &SourceRouter::SourceChanged, [&](const std::string& newSource) {
        if (!m_activeSource.empty() && m_playlist->HasTracks()) {
            std::string currentTrackId = m_playlist->GetCurrentTrack().id;
            int currentIndex = m_playlist->GetCurrentAbsoluteIndex();
            double currentPos = m_audio->GetPositionSeconds();
            m_dbManager->SaveSourceSession(m_activeSource, currentTrackId, currentIndex, currentPos);
        }
        m_activeSource = newSource;
        m_playbackCtrl->ClearState();
        m_isPlaybackStarted = false;
        m_playlist->Clear();
        QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("General/source", QString::fromStdString(newSource));
        m_console->SetCurrentProvider(m_router->GetCurrentProvider());
        m_playbackCtrl->SetCurrentProvider(m_router->GetCurrentProvider());
    });

    connect(m_router.get(), &SourceRouter::AuthUiStateChanged, [&](bool isWaiting) {
        m_console->SetState(isWaiting ? ConsoleState::WAITING_TOKEN_URL : ConsoleState::COMMAND_MODE);
    });

    connect(m_router.get(), &SourceRouter::ProviderReady, [&](bool isOnline) {
        m_vkSyncIndex = 0;
        InitPlaylistAndStart(isOnline);
    });

    // Обработка данных от провайдеров
    auto bindProvider = [&](IAudioProvider* client) {
        if (!client) return;
        connect(client, &IAudioProvider::AudioFetched, [&, client](const std::vector<Track>& t) {
            if (m_router->GetCurrentProvider() == client) OnAudioFetched(t);
        });
        connect(client, &IAudioProvider::FinishedFetching, [&, client]() {
            if (m_router->GetCurrentProvider() == client) OnFinishedFetching();
        });
    };

    m_playbackCtrl->SetProviderResolver([this](const std::string& source) {
        return m_router->GetProvider(source);
    });

    bindProvider(m_router->GetVkClient());
    bindProvider(m_router->GetSpotifyClient());
    bindProvider(m_router->GetSoundCloudClient());
    bindProvider(m_router->GetYandexClient());
    bindProvider(m_router->GetYouTubeClient());
}

void ApplicationCore::InitPlaylistAndStart(bool isOnline) {
    if (m_isPlaybackStarted) return;
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();

    int savePosMode = 2; // 0 = off, 1 = track only, 2 = track + time
    QVariant val = settings.value("Playback/SavePosition", 2);
    if (val.typeId() == QMetaType::Bool) {
        savePosMode = val.toBool() ? 2 : 1;
    } else {
        bool ok = false;
        int m = val.toInt(&ok);
        if (ok) savePosMode = std::clamp(m, 0, 2);
    }

    std::optional<SourceSession> sessionOpt;
    if (savePosMode > 0) {
        sessionOpt = m_dbManager->LoadSourceSession(m_activeSource);
    }

    if (!m_playlist->HasTracks()) {
        std::vector<Track> cachedTracks;
        if (m_activeSource == "All") {
            cachedTracks = m_dbManager->LoadAllSourcesTracks();
        } else if (m_activeSource.rfind("Custom:", 0) == 0) {
            std::string plName = m_activeSource.substr(7);
            int plId = -1;
            cachedTracks = m_dbManager->LoadPlaylistTracksByName(plName, plId);
        } else {
            cachedTracks = m_dbManager->LoadTracks(m_activeSource);
        }

        for (const auto& t : cachedTracks) {
            if (isOnline || QFile::exists(PathManager::GetDownloadFilePath(t.GetSafeFilename(), "mp3")) || QFile::exists(PathManager::GetDownloadFilePath(t.GetSafeFilename(), "aac"))) {
                m_playlist->AddTrack(t);
            }
        }
        if (isShuffle) {
            std::vector<std::string> savedQueue = m_dbManager->LoadQueueIds(m_activeSource, true);
            if (!savedQueue.empty()) m_playlist->RestoreShuffleQueue(savedQueue);
            else m_playlist->SetShuffle(true);
        }
    }

    if (m_playlist->HasTracks()) {
        m_isPlaybackStarted = true;

        int targetIndex = -1;
        if (savePosMode > 0 && sessionOpt.has_value()) {
            const auto& session = sessionOpt.value();
            const auto& allTracks = m_playlist->GetAllTracks();

            // 1. Поиск по trackId
            if (!session.trackId.empty()) {
                for (size_t i = 0; i < allTracks.size(); ++i) {
                    if (allTracks[i].id == session.trackId) {
                        targetIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            // 2. Фолбэк на trackIndex
            if (targetIndex < 0 && session.trackIndex >= 0 && session.trackIndex < static_cast<int>(allTracks.size())) {
                targetIndex = session.trackIndex;
            }

            // 3. Выставляем сохраненную позицию, если режим 2
            if (savePosMode == 2 && session.positionSeconds > 0.0) {
                m_playbackCtrl->SetSavedPosition(session.positionSeconds);
            } else {
                m_playbackCtrl->SetSavedPosition(0.0);
            }
        } else {
            m_playbackCtrl->SetSavedPosition(0.0);
        }

        if (targetIndex >= 0 && targetIndex < static_cast<int>(m_playlist->GetAllTracks().size())) {
            m_playlist->JumpTo(targetIndex);
        } else {
            m_playlist->OnTrackRequested(m_playlist->GetCurrentTrack());
        }
    } else {
        Logger::Log(LogLevel::WARNING, "[Оффлайн] Нет скачанных треков. Плеер пуст.");
    }
}

void ApplicationCore::OnAudioFetched(const std::vector<Track>& tracks) {
    bool hasNewTracks = false;
    auto allTracks = m_playlist->GetAllTracks();

    std::unordered_set<std::string> existingIds;
    existingIds.reserve(allTracks.size());
    for (const auto& c : allTracks) {
        existingIds.insert(c.id);
    }

    for (const auto& track : tracks) {
        if (existingIds.find(track.id) == existingIds.end()) {
            m_playlist->InsertTrack(m_vkSyncIndex, track);
            existingIds.insert(track.id); // Защита от дублей внутри самого чанка
            hasNewTracks = true;
        }
        m_vkSyncIndex++;
    }

    m_dbManager->SaveTracks(tracks);
    if (!m_isPlaybackStarted) InitPlaylistAndStart(true);

    if (!m_isPlaybackStarted || hasNewTracks) {
        m_dbManager->SaveQueue(m_playlist->GetAllTracks(), m_activeSource, false);
        m_dbManager->SaveQueue(m_playlist->GetQueueTracks(), m_activeSource, m_playlist->IsShuffle());
        m_dbManager->ExportQueueToTxt(m_playlist->GetQueueTracks(), "playlist.txt", m_playlist->IsShuffle());
    }
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

    // Активный сервис отключен — останавливаем воспроизведение и очищаем очередь
    m_playbackCtrl->ClearState();
    m_audio->Pause();
    m_isPlaybackStarted = false;
    m_playlist->Clear();

    QFile::remove(PathManager::GetPlaylistExportPath("playlist.txt"));

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.remove("Session/CurrentTrackIndex");
    settings.remove("Session/Position");
    settings.sync();

    m_router->FindNextAuthorizedSource(canonicalSvc, [this, canonicalSvc](const std::string& nextSource) {
        if (nextSource != "Offline") {
            m_console->SetStatusMessage("[Выход] Переключение на авторизованный сервис: " + nextSource);
            m_router->SwitchSource(nextSource);
        } else {
            m_console->SetStatusMessage("[Выход] Авторизованных аккаунтов не найдено. Переключено в Оффлайн режим.");
            m_router->SwitchSource("Offline");
        }
    });
}