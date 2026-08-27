#include "ApplicationCore.h"
#include <QCoreApplication>
#include <QSettings>
#include <QFile>
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
    SaveSession();
}

bool ApplicationCore::Initialize() {
    EnsureDefaultConfig();

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_activeSource = settings.value("General/source", "VK").toString().toStdString();
    int uiMode = settings.value("Visualizer/Mode", 0).toInt();

    // Запрет логирования в консоль для Ultimate режима
    Logger::SetConsoleOutputEnabled(uiMode == 0);

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
        settings.setValue("Session/Position", 0.0);
        settings.setValue("Session/Repeat", 1);
        settings.setValue("Session/CurrentTrackIndex", -1);
        settings.setValue("General/source", "VK");
        settings.setValue("Ui/ShowVisualizer", true);
        settings.setValue("Visualizer/Mode", 1);
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

    m_playbackCtrl->SetSavedPosition(settings.value("Session/Position", 0.0).toDouble());

    m_playbackCtrl->SetStartPaused(!settings.value("Session/AutoPlay", false).toBool());
}

void ApplicationCore::SaveSession() {
    Logger::Log(LogLevel::INFO, "ApplicationCore: Saving session state...");
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/Volume", m_audio->GetVolume());
    settings.setValue("Session/CurrentTrackIndex", m_playlist->GetCurrentAbsoluteIndex());
    settings.setValue("Session/Position", m_audio->GetPositionSeconds());
    settings.setValue("Session/Shuffle", m_playlist->IsShuffle());
    settings.setValue("Session/Repeat", m_playlist->GetRepeatMode());
    settings.sync();
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

    m_console->OnGaplessModeChanged = [&](bool isCrossfade) {
        QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Audio/CrossfadePlayback", isCrossfade);
        m_playbackCtrl->SetCrossfadeEnabled(isCrossfade);
        Logger::Log(LogLevel::INFO, std::string("Crossfade transition set to ") + (isCrossfade ? "ON" : "OFF"));
    };

    // Роутер событий
    connect(m_router.get(), &SourceRouter::SourceChanged, [&](const std::string& newSource) {
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
        connect(client, &IAudioProvider::AudioFetched, [&](const std::vector<Track>& t) { if (m_router->GetCurrentProvider() == client) OnAudioFetched(t); });
        connect(client, &IAudioProvider::FinishedFetching, [&, client]() { if (m_router->GetCurrentProvider() == client) OnFinishedFetching(); });
    };

    bindProvider(m_router->GetVkClient());
    bindProvider(m_router->GetSpotifyClient());
    bindProvider(m_router->GetSoundCloudClient());
    bindProvider(m_router->GetYandexClient());
}

void ApplicationCore::InitPlaylistAndStart(bool isOnline) {
    if (m_isPlaybackStarted) return;
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    bool isShuffle = settings.value("Session/Shuffle", false).toBool();
    int savedTrackIndex = settings.value("Session/CurrentTrackIndex", -1).toInt();

    if (!m_playlist->HasTracks()) {
        std::vector<Track> cachedTracks = m_dbManager->LoadTracks(m_activeSource);
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
        if (settings.value("Visualizer/Mode", 0).toInt() == 0) {
            std::cout << "\r\033[2K=== ПЛЕЕР ГОТОВ К РАБОТЕ ===\nРежим: " << (isShuffle ? "Шафл" : "Стандартный") << "\nВведите 'h' для справки\n\n> ";
            std::cout.flush();
        }
        m_isPlaybackStarted = true;
        if (savedTrackIndex >= 0 && savedTrackIndex < m_playlist->GetAllTracks().size()) {
            m_playlist->JumpTo(savedTrackIndex);
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