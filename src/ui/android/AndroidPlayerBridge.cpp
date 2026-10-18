#include "AndroidPlayerBridge.h"
#include "TrackListModel.h"
#include "core/ApplicationCore.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/auth/oauth/OAuthManager.h"
#include "core/audio/playback/PlaybackController.h"
#include "core/api/vk/VkAuthService.h"
#include "core/api/vk/VkClient.h"
#include "core/api/yandex/YandexClient.h"
#include "core/api/soundcloud/SoundCloudClient.h"
#include "core/api/youtube/YouTubeClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "utils/logger/Logger.h"
#include "services/database/DatabaseManager.h"
#include "core/lyrics/LyricsFetcher.h"
#include "services/downloader/TrackDownloader.h"
#include "core/api/IAudioProvider.h"
#include "utils/path/PathManager.h"
#include "services/config/ConfigurationService.h"
#include "core/shazam/AudioCaptureService.h"
#include "core/shazam/ShazamFFI.h"
#include "services/cache/CoverCacheService.h"

#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <QMetaObject>
#include <QGuiApplication>
#include <QClipboard>
#include <QTime>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QThreadPool>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace ui::android {

AndroidPlayerBridge::AndroidPlayerBridge(ApplicationCore& core, TrackListModel& trackModel, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_trackModel(trackModel) {

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(250);
    connect(m_pollTimer, &QTimer::timeout, this, &AndroidPlayerBridge::pollAudioState);
    m_pollTimer->start();

    m_volume = m_core.GetAudio().GetVolume();
    m_isShuffleCached = m_core.GetPlaylist().IsShuffle();
    m_repeatModeCached = m_core.GetPlaylist().GetRepeatMode();

    m_audioCapture = std::make_unique<AudioCaptureService>();
    if (m_core.GetConfigService()) {
        m_core.GetAudio().SetCrossfadeEnabled(m_core.GetConfigService()->GetCrossfadeEnabled());
        m_core.GetAudio().SetCrossfadeDurationMs(m_core.GetConfigService()->GetCrossfadeDurationMs());
        m_core.GetPlaybackCtrl().SetCrossfadeEnabled(m_core.GetConfigService()->GetCrossfadeEnabled());
    }

    connect(&services::cache::CoverCacheService::Instance(), &services::cache::CoverCacheService::CoverCached,
            this, [this](const QString& remoteUrl, const QString& localFileUrl) {
        if (!m_currentRemoteCoverUrl.isEmpty() && m_currentRemoteCoverUrl == remoteUrl) {
            m_currentCoverUrl = localFileUrl;
            emit currentTrackChanged();
        }
    });

    // Hook up real-time in-app logging
    Logger::SetLogCallback([this](LogLevel level, const std::string& message) {
        QString levelStr;
        switch (level) {
            case LogLevel::DEBUG:   levelStr = "DEBUG"; break;
            case LogLevel::INFO:    levelStr = "INFO"; break;
            case LogLevel::WARNING: levelStr = "WARN"; break;
            case LogLevel::ERROR:   levelStr = "ERROR"; break;
        }
        QString timeStr = QTime::currentTime().toString("hh:mm:ss");
        QString line = QString("[%1] [%2] %3").arg(timeStr, levelStr, QString::fromStdString(message));

        QMetaObject::invokeMethod(this, [this, line]() {
            addLogLine(line);
        }, Qt::QueuedConnection);
    });

    // Wire native VK authentication service
    auto* vkAuth = m_core.GetRouter().GetVkAuthService();
    if (vkAuth) {
        connect(vkAuth, &VkAuthService::StatusChanged, this, [this](const QString& status) {
            m_authStatusMessage = status;
            emit authStatusMessageChanged(status);
        });
        connect(vkAuth, &VkAuthService::PasswordRequired, this, [this](const QString& /*sid*/) {
            m_authStep = "need_password";
            m_authStatusMessage = "Введите пароль от вашей учетной записи VK";
            emit authStepChanged(m_authStep);
            emit authStatusMessageChanged(m_authStatusMessage);
            openAuthDialog();
        });
        connect(vkAuth, &VkAuthService::CodeRequired, this, [this](const QString& method, const QString& /*sid*/, const QString& mask) {
            m_authStep = "need_code";
            m_authMethod = method;
            m_authPhoneMask = mask;
            QString msg = "Введите код подтверждения";
            if (!mask.isEmpty()) msg += " (" + mask + ")";
            m_authStatusMessage = msg;
            emit authStepChanged(m_authStep);
            emit authStatusMessageChanged(m_authStatusMessage);
            openAuthDialog();
        });
        connect(vkAuth, &VkAuthService::CaptchaRequired, this, [this](const QString& /*redirectUri*/, const QString& captchaImg, const QString& captchaSid) {
            m_authStep = "need_captcha";
            m_authCaptchaImg = captchaImg;
            m_authCaptchaSid = captchaSid;
            m_authStatusMessage = "Введите символы с картинки (капча)";
            emit authStepChanged(m_authStep);
            emit authStatusMessageChanged(m_authStatusMessage);
            openAuthDialog();
        });
        connect(vkAuth, &VkAuthService::AuthSuccess, this, [this](const std::string& /*token*/, const std::string& /*secret*/, int /*userId*/) {
            m_authStep = "success";
            m_authError.clear();
            m_isAuthorized = true;
            m_authStatusMessage = "Вход в VK успешно выполнен!";
            emit isAuthorizedChanged(true);
            emit authStepChanged(m_authStep);
            emit authStatusMessageChanged(m_authStatusMessage);
            emit authSuccess("VK");
            SetStatus("Авторизация VK успешна!");
            if (m_currentSource.toLower() == "vk") {
                loadMyTracks();
            }
        });
        connect(vkAuth, &VkAuthService::AuthError, this, [this](const QString& err) {
            m_authStep = "error";
            m_authError = err;
            m_authStatusMessage = err;
            emit authStepChanged(m_authStep);
            emit authStatusMessageChanged(m_authStatusMessage);
            SetStatus("Ошибка VK: " + err);
        });
    }

    // Listen to router auth signals
    connect(&m_core.GetRouter(), &SourceRouter::SourceAuthRequired, this, [this](const std::string& src) {
        QString s = QString::fromStdString(src).toLower();
        if (s == m_currentSource.toLower()) {
            m_isAuthorized = false;
            emit isAuthorizedChanged(false);
            emit authRequired(QString::fromStdString(src));
        }
    });

    connect(&m_core.GetRouter(), &SourceRouter::SourceAuthSuccess, this, [this](const std::string& src) {
        QString s = QString::fromStdString(src).toLower();
        if (s == m_currentSource.toLower()) {
            m_isAuthorized = true;
            emit isAuthorizedChanged(true);
            emit authSuccess(QString::fromStdString(src));
        }
        if (m_isWebViewAuthOpen && (s == m_webViewAuthService.toLower() || s == m_currentSource.toLower())) {
            closeWebViewAuth();
            loadMyTracks();
        }
    });

    connect(&m_core.GetRouter(), &SourceRouter::WebViewAuthRequested, this, [this](const QString& service, const QString& url) {
        m_webViewAuthService = service;
        m_webViewAuthUrl = url;
        emit webViewAuthServiceChanged(service);
        emit webViewAuthUrlChanged(url);
        closeAuthDialog();
        setWebViewAuthOpen(true);
    });

    // Initialize caches asynchronously without blocking GUI thread
    updateCustomPlaylistsCache();
    updateAudioCacheUsedMb();

    // Pre-load cached tracks asynchronously from SQLite for instant, non-blocking startup
    loadTracksForSourceAsync(m_currentSource, false);

    // Check initial auth state for active source
    checkCurrentSourceAuth();
}

AndroidPlayerBridge::~AndroidPlayerBridge() {
    Logger::SetLogCallback(nullptr);
}

void AndroidPlayerBridge::addLogLine(const QString& line) {
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logLines.push_back(line);
        if (m_logLines.size() > 500) {
            m_logLines.pop_front();
        }
    }
    emit logsUpdated();
    emit newLogLine(line);
}

QString AndroidPlayerBridge::fullLogs() const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    QString result;
    for (const auto& line : m_logLines) {
        result += line + "\n";
    }
    return result;
}

void AndroidPlayerBridge::clearLogs() {
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logLines.clear();
    }
    emit logsUpdated();
    SetStatus("Журнал логов очищен");
}

void AndroidPlayerBridge::refreshLogs() {
    emit logsUpdated();
}

void AndroidPlayerBridge::copyLogsToClipboard() {
    QClipboard* cb = QGuiApplication::clipboard();
    if (cb) {
        cb->setText(fullLogs());
        SetStatus("Логи скопированы в буфер обмена");
    }
}

bool AndroidPlayerBridge::isShuffle() const {
    return m_core.GetPlaylist().IsShuffle();
}

void AndroidPlayerBridge::toggleShuffle() {
    setShuffle(!isShuffle());
}

void AndroidPlayerBridge::setShuffle(bool enable) {
    if (m_core.GetPlaylist().IsShuffle() != enable) {
        m_core.GetPlaylist().SetShuffle(enable);
        m_isShuffleCached = enable;
        emit shuffleChanged(enable);
        if (m_core.GetConfigService()) {
            m_core.GetConfigService()->SetShuffle(enable);
        }
        SetStatus(enable ? "Режим: Шафл (случайный порядок)" : "Режим: По очереди (по порядку)");
        m_core.GetDbManager().SaveQueue(m_core.GetPlaylist().GetQueueTracks(), m_currentSource.toStdString(), enable);

        if (m_activeTabIndex == 0) {
            if (enable) {
                m_trackModel.SetTracks(m_core.GetPlaylist().GetQueueTracks());
            } else {
                m_trackModel.SetTracks(m_core.GetPlaylist().GetAllTracks());
            }
            const auto& cur = m_core.GetPlaylist().GetCurrentTrack();
            if (!cur.id.empty()) {
                UpdateCurrentTrack(cur);
            }
        }
    }
}

int AndroidPlayerBridge::repeatMode() const {
    return m_core.GetPlaylist().GetRepeatMode();
}

void AndroidPlayerBridge::toggleRepeat() {
    m_core.GetPlaylist().ToggleRepeat();
    int mode = repeatMode();
    m_repeatModeCached = mode;
    emit repeatModeChanged(mode);
    if (mode == 1) SetStatus("Режим повтора: Все треки (All)");
    else if (mode == 2) SetStatus("Режим повтора: Один трек (One)");
    else SetStatus("Режим повтора: Выключен (Off)");
}

void AndroidPlayerBridge::setRepeatMode(int mode) {
    if (m_core.GetPlaylist().GetRepeatMode() != mode) {
        m_core.GetPlaylist().SetRepeatMode(mode);
        m_repeatModeCached = mode;
        emit repeatModeChanged(mode);
    }
}

void AndroidPlayerBridge::fetchLyrics() {
    Track cur = m_core.GetPlaylist().GetCurrentTrack();
    if (cur.title.empty()) {
        SetStatus("Сейчас никакой трек не играет");
        return;
    }
    if (!cur.lyrics.empty()) {
        m_currentLyrics = QString::fromStdString(cur.lyrics);
        emit currentLyricsChanged();
        SetStatus("Текст песни получен из кэша");
        return;
    }
    SetStatus(QString("Поиск текста: %1 - %2...").arg(QString::fromStdString(cur.artist), QString::fromStdString(cur.title)));
    m_core.GetLyricsFetcher().FetchLyrics(cur.artist, cur.title, [this, cur](const std::string& lyrics) {
        QMetaObject::invokeMethod(this, [this, cur, lyrics]() {
            if (!lyrics.empty()) {
                m_currentLyrics = QString::fromStdString(lyrics);
                emit currentLyricsChanged();
                m_core.GetDbManager().UpdateTrackLyrics(cur.id, lyrics);
                SetStatus("Текст песни успешно получен");
            } else {
                m_currentLyrics = "Текст для этого трека не найден.";
                emit currentLyricsChanged();
                SetStatus("Текст песни не найден");
            }
        }, Qt::QueuedConnection);
    });
}

bool AndroidPlayerBridge::isCurrentFavorite() const {
    Track cur = m_core.GetPlaylist().GetCurrentTrack();
    return cur.isLibrary;
}

void AndroidPlayerBridge::toggleCurrentFavorite() {
    Track cur = m_core.GetPlaylist().GetCurrentTrack();
    if (cur.title.empty()) return;
    int idx = m_core.GetPlaylist().GetCurrentAbsoluteIndex();
    if (cur.isLibrary) {
        removeTrackFromFavorites(idx);
    } else {
        addTrackToFavorites(idx);
    }
}

void AndroidPlayerBridge::downloadCurrentTrack() {
    Track cur = m_core.GetPlaylist().GetCurrentTrack();
    if (cur.title.empty()) {
        SetStatus("Сейчас никакой трек не играет");
        return;
    }
    if (!m_core.GetRouter().GetOrCreateProvider(cur.source)) {
        SetStatus("Провайдер недоступен для скачивания");
        return;
    }
    IAudioProvider* prov = m_core.GetRouter().GetOrCreateProvider(cur.source);
    SetStatus(QString("Получение ссылки для скачивания: %1...").arg(QString::fromStdString(cur.title)));

    prov->FetchTrackUrl(cur.id, [this, cur](const std::string& url, bool err) {
        QMetaObject::invokeMethod(this, [this, cur, url, err]() {
            if (!err && !url.empty()) {
                m_core.GetDownloader().Download(cur, url);
                SetStatus(QString("Скачивание начато: %1 - %2").arg(QString::fromStdString(cur.artist), QString::fromStdString(cur.title)));
            } else {
                SetStatus("Не удалось получить ссылку для скачивания");
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::downloadTrack(int index) {
    const Track* tPtr = m_trackModel.GetTrackAt(index);
    if (!tPtr) return;
    Track track = *tPtr;

    if (!m_core.GetRouter().GetOrCreateProvider(track.source)) {
        SetStatus("Провайдер недоступен для скачивания");
        return;
    }
    IAudioProvider* prov = m_core.GetRouter().GetOrCreateProvider(track.source);
    SetStatus(QString("Получение ссылки для скачивания: %1...").arg(QString::fromStdString(track.title)));

    prov->FetchTrackUrl(track.id, [this, track](const std::string& url, bool err) {
        QMetaObject::invokeMethod(this, [this, track, url, err]() {
            if (!err && !url.empty()) {
                m_core.GetDownloader().Download(track, url);
                SetStatus(QString("Скачивание начато: %1 - %2").arg(QString::fromStdString(track.artist), QString::fromStdString(track.title)));
            } else {
                SetStatus("Не удалось получить ссылку для скачивания");
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::deleteDownloadedTrack(int index) {
    const Track* tPtr = m_trackModel.GetTrackAt(index);
    if (!tPtr) return;
    Track track = *tPtr;
    QString mp3 = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "mp3");
    QString aac = PathManager::GetDownloadFilePath(track.GetSafeFilename(), "aac");
    bool removed = false;
    if (QFile::exists(mp3)) removed = QFile::remove(mp3);
    if (QFile::exists(aac)) removed = QFile::remove(aac);
    if (removed) {
        SetStatus(QString("Файл трека удален: %1").arg(QString::fromStdString(track.title)));
        if (m_currentSource == "Offline") {
            loadMyTracks();
        }
    } else {
        SetStatus("Локальный файл трека не найден");
    }
}

QStringList AndroidPlayerBridge::customPlaylists() const {
    return m_customPlaylistsCached;
}

void AndroidPlayerBridge::updateCustomPlaylistsCache() {
    QPointer<AndroidPlayerBridge> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis]() {
        if (!safeThis) return;
        QStringList res;
        for (const auto& pl : safeThis->m_core.GetDbManager().GetPlaylists()) {
            res.push_back(QString::fromStdString(pl.name));
        }
        QMetaObject::invokeMethod(safeThis, [safeThis, res]() {
            if (!safeThis) return;
            safeThis->m_customPlaylistsCached = res;
            emit safeThis->customPlaylistsChanged();
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::createPlaylist(const QString& name) {
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    QPointer<AndroidPlayerBridge> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis, trimmed]() {
        if (!safeThis) return;
        bool ok = safeThis->m_core.GetDbManager().CreatePlaylist(trimmed.toStdString());
        QMetaObject::invokeMethod(safeThis, [safeThis, trimmed, ok]() {
            if (!safeThis) return;
            if (ok) {
                safeThis->updateCustomPlaylistsCache();
                safeThis->SetStatus(QString("Плейлист \"%1\" создан").arg(trimmed));
            } else {
                safeThis->SetStatus("Не удалось создать плейлист");
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::deletePlaylist(const QString& name) {
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    QPointer<AndroidPlayerBridge> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis, trimmed]() {
        if (!safeThis) return;
        int plId = -1;
        safeThis->m_core.GetDbManager().LoadPlaylistTracksByName(trimmed.toStdString(), plId);
        bool ok = (plId != -1 && safeThis->m_core.GetDbManager().DeletePlaylist(plId));
        QMetaObject::invokeMethod(safeThis, [safeThis, trimmed, ok]() {
            if (!safeThis) return;
            if (ok) {
                safeThis->updateCustomPlaylistsCache();
                safeThis->SetStatus(QString("Плейлист \"%1\" удален").arg(trimmed));
                if (safeThis->m_currentSource == "Custom:" + trimmed) {
                    safeThis->switchSource("VK");
                }
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::addTrackToPlaylist(const QString& playlistName, int trackIndex) {
    const Track* tPtr = m_trackModel.GetTrackAt(trackIndex);
    if (!tPtr) return;
    int plId = -1;
    m_core.GetDbManager().LoadPlaylistTracksByName(playlistName.toStdString(), plId);
    if (plId != -1) {
        if (m_core.GetDbManager().AddTrackToPlaylist(plId, tPtr->id)) {
            SetStatus(QString("Трек \"%1\" добавлен в \"%2\"").arg(QString::fromStdString(tPtr->title), playlistName));
        } else {
            SetStatus("Трек уже находится в этом плейлисте");
        }
    }
}

void AndroidPlayerBridge::playCustomPlaylist(const QString& name) {
    switchSource("Custom:" + name.trimmed());
}

void AndroidPlayerBridge::removeTrackFromQueue(int index) {
    m_core.GetPlaylist().RemoveTrack(index);
    m_myTracks = m_core.GetPlaylist().GetAllTracks();
    for (auto& t : m_myTracks) t.isLibrary = true;
    m_trackModel.SetTracks(m_myTracks);
    SetStatus("Трек удален из текущей очереди");
}

void AndroidPlayerBridge::exportPlaylistTxt() {
    m_core.GetDbManager().ExportQueueToTxt(m_core.GetPlaylist().GetQueueTracks(), "playlist.txt", m_core.GetPlaylist().IsShuffle());
    QString path = PathManager::GetPlaylistExportPath("playlist.txt");
    SetStatus(QString("Плейлист экспортирован в %1").arg(path));
}

QString AndroidPlayerBridge::formattedPosition() const {
    qint64 totalSec = m_positionMs / 1000;
    qint64 m = totalSec / 60;
    qint64 s = totalSec % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

QString AndroidPlayerBridge::formattedDuration() const {
    qint64 totalSec = m_durationMs / 1000;
    qint64 m = totalSec / 60;
    qint64 s = totalSec % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

QStringList AndroidPlayerBridge::availableSources() const {
    return {"VK", "YouTube", "SoundCloud", "Yandex", "Spotify", "Offline"};
}

void AndroidPlayerBridge::setActiveTabIndex(int index) {
    if (m_activeTabIndex != index) {
        m_activeTabIndex = index;
        emit activeTabIndexChanged(index);

        if (index == 0) {
            // "Мои треки"
            m_trackModel.SetTracks(m_myTracks);
        }
    }
}

void AndroidPlayerBridge::UpdateCurrentTrack(const Track& track) {
    m_currentTitle = QString::fromStdString(track.title);
    m_currentArtist = QString::fromStdString(track.artist);
    m_currentRemoteCoverUrl = QString::fromStdString(track.GetDisplayCoverUrl(true));
    m_currentCoverUrl = services::cache::CoverCacheService::Instance().GetCoverUrl(m_currentRemoteCoverUrl, false);
    m_currentTrackSource = QString::fromStdString(track.source);
    m_durationMs = static_cast<qint64>(track.duration) * 1000;
    m_currentLyrics = QString::fromStdString(track.lyrics);

    emit currentTrackChanged();
    emit durationChanged(m_durationMs);
    emit currentLyricsChanged();

    // Update playing indicator in list model if track matches
    const auto& tracks = m_trackModel.GetTracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (!track.id.empty() && tracks[i].id == track.id) {
            m_trackModel.SetPlayingIndex(static_cast<int>(i));
            break;
        }
    }
}

void AndroidPlayerBridge::SetStatus(const QString& msg) {
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged(msg);
    }
}

void AndroidPlayerBridge::OnAudioFetched(const std::vector<Track>& /*tracks*/) {
    if (m_activeTabIndex == 0) {
        m_myTracks = m_core.GetPlaylist().GetAllTracks();
        for (auto& t : m_myTracks) {
            t.isLibrary = true;
        }
        m_trackModel.SetTracks(m_myTracks);
    }
}

void AndroidPlayerBridge::OnFinishedFetching() {
    if (m_activeTabIndex == 0) {
        m_myTracks = m_core.GetPlaylist().GetAllTracks();
        for (auto& t : m_myTracks) {
            t.isLibrary = true;
        }
        m_trackModel.SetTracks(m_myTracks);
    }
    SetStatus("Загрузка треков завершена");
}

void AndroidPlayerBridge::play() {
    m_core.GetAudio().Resume();
}

void AndroidPlayerBridge::pause() {
    m_core.GetAudio().Pause();
}

void AndroidPlayerBridge::togglePlay() {
    if (m_core.GetAudio().IsPlaying()) {
        m_core.GetAudio().Pause();
    } else {
        m_core.GetAudio().Resume();
    }
}

void AndroidPlayerBridge::seek(qint64 positionMs) {
    double sec = static_cast<double>(positionMs) / 1000.0;
    m_core.GetAudio().SetPositionSeconds(sec);
}

void AndroidPlayerBridge::setVolume(float v) {
    float clamped = std::clamp(v, 0.0f, 1.0f);
    if (std::abs(m_volume - clamped) > 0.001f) {
        m_volume = clamped;
        m_core.GetAudio().SetVolume(m_volume);
        emit volumeChanged(m_volume);
    }
}

void AndroidPlayerBridge::nextTrack() {
    m_core.GetPlaylist().Next();
}

void AndroidPlayerBridge::prevTrack() {
    m_core.GetPlaylist().Previous();
}

void AndroidPlayerBridge::search(const QString& query) {
    QString q = query.trimmed();
    if (q.isEmpty()) return;

    m_isSearching = true;
    emit isSearchingChanged(true);

    m_activeTabIndex = 1; // Переключаемся на вкладку результатов поиска
    emit activeTabIndexChanged(1);

    std::string searchScope = m_searchSource.toStdString();
    if (searchScope.empty()) searchScope = "All";

    SetStatus(QString("Поиск \"%1\" (%2)...").arg(q, QString::fromStdString(searchScope)));

    int gen = ++m_currentSearchGen;
    QPointer<AndroidPlayerBridge> safeThis(this);

    // 1. Асинхронный поиск в локальной базе данных SQLite через QThreadPool
    QThreadPool::globalInstance()->start([safeThis, q, searchScope, gen]() {
        if (!safeThis || safeThis->m_currentSearchGen != gen) return;

        std::vector<Track> libraryMatches;
        auto allLibraryTracks = (searchScope == "All") ?
            safeThis->m_core.GetDbManager().LoadAllSourcesTracks() :
            safeThis->m_core.GetDbManager().LoadTracks(searchScope);

        std::unordered_set<std::string> libraryIds;
        QString qLower = q.toLower();
        for (auto& t : allLibraryTracks) {
            libraryIds.insert(t.id);
            t.isLibrary = true;
            QString artist = QString::fromStdString(t.artist).toLower();
            QString title = QString::fromStdString(t.title).toLower();
            if (artist.contains(qLower) || title.contains(qLower)) {
                libraryMatches.push_back(t);
            }
        }

        QMetaObject::invokeMethod(safeThis, [safeThis, q, searchScope, libraryMatches, libraryIds, gen]() {
            if (!safeThis || safeThis->m_currentSearchGen != gen) return;

            // Мгновенно отображаем найденные в медиатеке треки
            if (!libraryMatches.empty()) {
                safeThis->m_trackModel.SetTracks(libraryMatches);
            }

            // 2. Сетевой поиск через роутер
            safeThis->m_core.GetRouter().Search(searchScope, q.toStdString(), 50, 0,
                [safeThis, q, libraryMatches, libraryIds, gen](const std::vector<Track>& onlineTracks, const std::string& error) {
                    QMetaObject::invokeMethod(safeThis, [safeThis, q, libraryMatches, libraryIds, onlineTracks, error, gen]() {
                        if (!safeThis || safeThis->m_currentSearchGen != gen) return;

                        safeThis->m_isSearching = false;
                        emit safeThis->isSearchingChanged(false);

                        std::vector<Track> combinedResults = libraryMatches;
                        std::unordered_set<std::string> seenIds;
                        for (const auto& t : combinedResults) {
                            seenIds.insert(t.id);
                        }

                        for (auto t : onlineTracks) {
                            if (seenIds.find(t.id) != seenIds.end()) {
                                continue;
                            }
                            seenIds.insert(t.id);
                            t.isLibrary = (libraryIds.find(t.id) != libraryIds.end());
                            combinedResults.push_back(t);
                        }

                        if (!error.empty() && combinedResults.empty()) {
                            safeThis->SetStatus(QString("Ошибка поиска: %1").arg(QString::fromStdString(error)));
                        } else {
                            safeThis->SetStatus(QString("Найдено: %1 в медиатеке, %2 в сети")
                                .arg(libraryMatches.size())
                                .arg(combinedResults.size() - libraryMatches.size()));
                            safeThis->m_trackModel.SetTracks(combinedResults);
                        }
                    }, Qt::QueuedConnection);
                });
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::playTrackAtIndex(int index) {
    const auto& currentTracks = m_trackModel.GetTracks();
    if (index < 0 || index >= static_cast<int>(currentTracks.size())) {
        Logger::Log(LogLevel::WARNING, "AndroidPlayerBridge: Track at index " + std::to_string(index) + " out of bounds!");
        return;
    }

    const Track& track = currentTracks[index];
    Logger::Log(LogLevel::INFO, "AndroidPlayerBridge: Playing track at index " + std::to_string(index) + ": " + track.title);

    m_core.GetPlaybackCtrl().SetStartPaused(false);

    if (m_core.GetPlaylist().IsShuffle()) {
        m_trackModel.SetPlayingIndex(index);
        m_core.GetPlaylist().JumpToQueueIndex(index);
    } else {
        // Синхронизируем весь текущий список в PlaylistManager только если список изменился
        bool tracksChanged = false;
        const auto& existing = m_core.GetPlaylist().GetAllTracks();
        if (existing.size() != currentTracks.size()) {
            tracksChanged = true;
        } else {
            for (size_t i = 0; i < existing.size(); ++i) {
                if (existing[i].id != currentTracks[i].id) {
                    tracksChanged = true;
                    break;
                }
            }
        }
        if (tracksChanged) {
            m_core.GetPlaylist().SetTracks(currentTracks);
        }
        m_trackModel.SetPlayingIndex(index);
        m_core.GetPlaylist().JumpTo(index);
    }
}

void AndroidPlayerBridge::loadTracksForSourceAsync(const QString& sourceName, bool isSwitchSource) {
    std::vector<Track> cached;
    if (sourceName == "Offline") {
        auto allTracks = m_core.GetDbManager().LoadTracks("Offline");
        QSet<QString> downloadedFiles;
        auto scanDir = [&](const QString& dirPath) {
            if (dirPath.isEmpty()) return;
            QDir dir(dirPath);
            if (dir.exists()) {
                const auto entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                for (const auto& f : entries) {
                    downloadedFiles.insert(f.toLower());
                }
            }
        };
        scanDir(PathManager::GetDownloadsDir());
        QString custom = PathManager::GetCustomDownloadsDir();
        if (!custom.isEmpty()) scanDir(custom);

        for (const auto& t : allTracks) {
            QString safeBase = QString::fromStdString(t.GetSafeFilename()).toLower();
            if (downloadedFiles.contains(safeBase + ".mp3") ||
                downloadedFiles.contains(safeBase + ".aac")) {
                cached.push_back(t);
            }
        }
    } else if (sourceName == "All" || sourceName == "Все" || sourceName == "Все источники") {
        cached = m_core.GetDbManager().LoadAllSourcesTracks();
    } else if (sourceName.startsWith("Custom:")) {
        std::string plName = sourceName.mid(7).toStdString();
        int plId = -1;
        cached = m_core.GetDbManager().LoadPlaylistTracksByName(plName, plId);
    } else {
        cached = m_core.GetDbManager().LoadTracks(sourceName.toStdString());
    }

    for (auto& t : cached) {
        t.isLibrary = true;
    }

    if (!cached.empty()) {
        m_myTracks = cached;
        if (m_activeTabIndex == 0) {
            m_trackModel.SetTracks(m_myTracks);
            m_core.GetPlaylist().SetTracks(m_myTracks);
        }
        SetStatus(QString("Загружено из кэша: %1 треков").arg(cached.size()));
    } else {
        m_myTracks.clear();
        if (m_activeTabIndex == 0) {
            m_trackModel.ClearTracks();
            m_core.GetPlaylist().Clear();
        }
        if (sourceName == "Offline") {
            SetStatus("Оффлайн: нет скачанных треков");
        }
    }

    if (isSwitchSource) {
        m_core.GetRouter().SwitchSource(sourceName.toStdString());
        checkCurrentSourceAuth();
    }
}

void AndroidPlayerBridge::switchSource(const QString& sourceName) {
    QString s = sourceName.trimmed();
    if (s.isEmpty()) return;

    QString lower = s.toLower();
    QString norm = "VK";
    if (lower == "vk") norm = "VK";
    else if (lower == "yandex") norm = "Yandex";
    else if (lower == "soundcloud" || lower == "sc") norm = "SoundCloud";
    else if (lower == "youtube" || lower == "yt") norm = "YouTube";
    else if (lower == "spotify") norm = "Spotify";
    else if (lower == "offline") norm = "Offline";
    else if (s.startsWith("Custom:", Qt::CaseInsensitive)) norm = "Custom:" + s.mid(7).trimmed();
    else norm = s;

    if (m_currentSource != norm) {
        m_currentSource = norm;
        emit currentSourceChanged(m_currentSource);
        SetStatus(QString("Переключение на источник: %1").arg(norm));
        loadTracksForSourceAsync(norm, true);
    }
}

void AndroidPlayerBridge::loadMyTracks() {
    setActiveTabIndex(0);
    SetStatus(QString("Загрузка треков из %1...").arg(m_currentSource));
    loadTracksForSourceAsync(m_currentSource, false);
    m_core.GetRouter().SwitchSource(m_currentSource.toStdString());
    checkCurrentSourceAuth();
}

void AndroidPlayerBridge::addTrackToFavorites(int index) {
    const Track* trackPtr = m_trackModel.GetTrackAt(index);
    if (!trackPtr) return;

    Track track = *trackPtr;
    SetStatus(QString("Добавление в «Мои треки»: %1 - %2...").arg(QString::fromStdString(track.artist), QString::fromStdString(track.title)));

    m_core.GetRouter().AddTrackToFavorites(track, [this, track](bool success, const std::string& err) {
        QMetaObject::invokeMethod(this, [this, track, success, err]() {
            if (success) {
                Track updated = track;
                updated.isLibrary = true;
                m_core.GetDbManager().SaveTracks({updated}, true);
                m_trackModel.UpdateTrackLibraryStatus(track.id, true);

                bool found = false;
                for (auto& t : m_myTracks) {
                    if (t.id == track.id) {
                        t.isLibrary = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_myTracks.insert(m_myTracks.begin(), updated);
                }

                SetStatus("Трек добавлен в «Мои треки»");
            } else {
                SetStatus(QString("Не удалось добавить трек: %1").arg(QString::fromStdString(err)));
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::removeTrackFromFavorites(int index) {
    const Track* trackPtr = m_trackModel.GetTrackAt(index);
    if (!trackPtr) return;

    Track track = *trackPtr;
    m_core.GetRouter().RemoveTrackFromFavorites(track, [this, track](bool success, const std::string& err) {
        QMetaObject::invokeMethod(this, [this, track, success, err]() {
            if (success) {
                QThreadPool::globalInstance()->start([this, trackId = track.id]() {
                    m_core.GetDbManager().SetTrackIsLibrary(trackId, false);
                });
                m_trackModel.UpdateTrackLibraryStatus(track.id, false);

                std::erase_if(m_myTracks, [&track](const Track& t) { return t.id == track.id; });
                SetStatus("Трек удален из «Мои треки»");
            } else {
                SetStatus(QString("Не удалось удалить: %1").arg(QString::fromStdString(err)));
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::setWebViewAuthOpen(bool open) {
    if (m_isWebViewAuthOpen != open) {
        m_isWebViewAuthOpen = open;
        emit webViewAuthOpenChanged(open);
    }
}

void AndroidPlayerBridge::setSearchSource(const QString& src) {
    if (m_searchSource != src) {
        m_searchSource = src;
        emit searchSourceChanged(src);
    }
}

void AndroidPlayerBridge::startWebViewAuth(const QString& service) {
    QString targetService = service.trimmed();
    if (targetService.isEmpty()) targetService = m_currentSource;

    QString lower = targetService.toLower();
    QString norm = "Yandex";
    if (lower == "yandex") norm = "Yandex";
    else if (lower == "soundcloud" || lower == "sc") norm = "SoundCloud";
    else if (lower == "youtube" || lower == "yt") norm = "YouTube";
    else if (lower == "spotify") norm = "Spotify";

    m_webViewAuthService = norm;
    emit webViewAuthServiceChanged(norm);

    m_core.GetRouter().SetCurrentAuthService(norm);

    QString url = "";
    if (norm == "Yandex") {
        url = "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d";
    } else if (norm == "SoundCloud") {
        url = "https://soundcloud.com/signin";
    } else if (norm == "YouTube") {
        url = "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F";
    } else if (norm == "Spotify") {
        auto* sp = m_core.GetRouter().GetSpotifyClient();
        if (sp) {
            QString clientId = "23cabbbdc6cd418abb4b39c32c41195d";
            url = QString::fromStdString(sp->StartAuthPkce(clientId));
        }
        if (url.isEmpty()) {
            url = "https://accounts.spotify.com/authorize";
        }
    }

    m_webViewAuthUrl = url;
    emit webViewAuthUrlChanged(url);

    closeAuthDialog();
    setWebViewAuthOpen(true);
}

void AndroidPlayerBridge::closeWebViewAuth() {
    setWebViewAuthOpen(false);
}

void AndroidPlayerBridge::onWebViewUrlChanged(const QString& url) {
    if (url.isEmpty()) return;
    auto* authManager = m_core.GetRouter().GetAuthManager();
    if (authManager) {
        authManager->onUrlIntercepted(url);
    }
}

void AndroidPlayerBridge::onScTokenIntercepted(const QString& token) {
    if (token.isEmpty()) return;
    auto* authManager = m_core.GetRouter().GetAuthManager();
    if (authManager) {
        authManager->onScTokenIntercepted(token);
    }
}

void AndroidPlayerBridge::onYtAuthIntercepted(const QString& cookies) {
    if (cookies.isEmpty()) return;
    auto* authManager = m_core.GetRouter().GetAuthManager();
    if (authManager) {
        authManager->onYtAuthIntercepted(cookies);
    }
}

void AndroidPlayerBridge::setAuthDialogOpen(bool open) {
    if (m_isAuthDialogOpen != open) {
        m_isAuthDialogOpen = open;
        emit authDialogOpenChanged(open);
    }
}

void AndroidPlayerBridge::openAuthDialog() {
    setAuthDialogOpen(true);
}

void AndroidPlayerBridge::closeAuthDialog() {
    setAuthDialogOpen(false);
}

void AndroidPlayerBridge::cancelAuth() {
    m_authStep = "idle";
    m_authError.clear();
    m_authStatusMessage.clear();
    emit authStepChanged(m_authStep);
    closeAuthDialog();
}

void AndroidPlayerBridge::startVkLogin(const QString& login, const QString& password) {
    VkAuthService* auth = m_core.GetRouter().GetVkAuthService();
    if (!auth) {
        m_authStep = "error";
        m_authError = "Служба авторизации VK недоступна";
        emit authStepChanged(m_authStep);
        return;
    }
    m_authStep = "authenticating";
    m_authError.clear();
    m_authStatusMessage = "Подключение к серверам VK...";
    emit authStepChanged(m_authStep);
    emit authStatusMessageChanged(m_authStatusMessage);
    SetStatus("Проверка учетной записи VK...");
    auth->StartLogin(login, password);
}

void AndroidPlayerBridge::submitVkPassword(const QString& password) {
    VkAuthService* auth = m_core.GetRouter().GetVkAuthService();
    if (auth) {
        m_authStep = "authenticating";
        m_authStatusMessage = "Проверка пароля...";
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        auth->SubmitPassword(password);
    }
}

void AndroidPlayerBridge::submitVkCode(const QString& code) {
    VkAuthService* auth = m_core.GetRouter().GetVkAuthService();
    if (auth) {
        m_authStep = "authenticating";
        m_authStatusMessage = "Проверка кода...";
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        auth->SubmitCode(code);
    }
}

void AndroidPlayerBridge::submitVkCaptcha(const QString& captchaKey) {
    VkAuthService* auth = m_core.GetRouter().GetVkAuthService();
    if (auth) {
        m_authStep = "authenticating";
        m_authStatusMessage = "Отправка капчи...";
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        auth->SubmitCaptchaKey(captchaKey);
    }
}

void AndroidPlayerBridge::loginVk(const QString& login, const QString& password, const QString& code2fa) {
    startVkLogin(login, password);
    if (!code2fa.isEmpty()) {
        submitVkCode(code2fa);
    }
}

void AndroidPlayerBridge::saveToken(const QString& service, const QString& token, const QString& secret) {
    QString svc = service.trimmed();
    if (svc.isEmpty()) svc = m_currentSource;
    QString lowerSvc = svc.toLower();
    std::string tokenStd = token.trimmed().toStdString();
    std::string secretStd = secret.trimmed().toStdString();

    if (tokenStd.empty()) {
        m_authStep = "error";
        m_authError = "Токен не может быть пустым";
        emit authStepChanged(m_authStep);
        return;
    }

    OAuthManager* oauth = m_core.GetRouter().GetAuthManager();
    if (!oauth) return;

    if (lowerSvc == "vk") {
        oauth->SaveToken(tokenStd, "VK");
        if (!secretStd.empty()) {
            oauth->SaveSecret(secretStd, "VK");
        }
        auto* vk = m_core.GetRouter().GetVkClient();
        if (vk) {
            vk->SetAccessToken(tokenStd);
            if (!secretStd.empty()) vk->SetSecret(secretStd);
            vk->ValidateToken([this, vk](bool ok) {
                QMetaObject::invokeMethod(this, [this, vk, ok]() {
                    if (ok) {
                        m_isAuthorized = true;
                        m_authStep = "success";
                        m_authStatusMessage = "Токен VK сохранен и проверен!";
                        emit isAuthorizedChanged(true);
                        emit authStepChanged(m_authStep);
                        emit authStatusMessageChanged(m_authStatusMessage);
                        emit authSuccess("VK");
                        vk->FetchAllUserAudio(0, 200);
                        SetStatus("Токен VK сохранен и подтвержден!");
                    } else {
                        m_authStep = "error";
                        m_authError = "Введенный токен VK недействителен";
                        emit authStepChanged(m_authStep);
                    }
                }, Qt::QueuedConnection);
            });
        }
    } else if (lowerSvc == "yandex") {
        oauth->SaveToken(tokenStd, "Yandex");
        oauth->ClearSavedUserId("Yandex");
        auto* ya = m_core.GetRouter().GetYandexClient();
        if (ya) {
            ya->SetAccessToken(tokenStd);
            ya->SetUserId("");
            ya->FetchAllUserAudio(0, 200);
        }
        m_isAuthorized = true;
        m_authStep = "success";
        m_authStatusMessage = "Токен Яндекс Музыки сохранен!";
        emit isAuthorizedChanged(true);
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        emit authSuccess("Yandex");
        SetStatus("Токен Яндекс Музыки сохранен!");
    } else if (lowerSvc == "soundcloud" || lowerSvc == "sc") {
        oauth->SaveToken(tokenStd, "SoundCloud");
        auto* sc = m_core.GetRouter().GetSoundCloudClient();
        if (sc) {
            sc->SetAccessToken(tokenStd);
            sc->InitializeWithToken();
        }
        m_isAuthorized = true;
        m_authStep = "success";
        m_authStatusMessage = "Токен SoundCloud сохранен!";
        emit isAuthorizedChanged(true);
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        emit authSuccess("SoundCloud");
        SetStatus("Токен SoundCloud сохранен!");
    } else if (lowerSvc == "youtube" || lowerSvc == "yt") {
        oauth->SaveToken(tokenStd, "YouTube");
        auto* yt = m_core.GetRouter().GetYouTubeClient();
        if (yt) {
            yt->SetAccessToken(tokenStd);
            yt->FetchAllUserAudio(0, 100);
        }
        m_isAuthorized = true;
        m_authStep = "success";
        m_authStatusMessage = "Куки YouTube Music сохранены!";
        emit isAuthorizedChanged(true);
        emit authStepChanged(m_authStep);
        emit authStatusMessageChanged(m_authStatusMessage);
        emit authSuccess("YouTube");
        SetStatus("Куки YouTube Music сохранены!");
    } else if (lowerSvc == "spotify") {
        oauth->SaveToken(tokenStd, "Spotify");
        auto* sp = m_core.GetRouter().GetSpotifyClient();
        if (sp) {
            sp->SetAccessToken(tokenStd);
            sp->FetchAllUserAudio(0, 50);
        }
        m_isAuthorized = true;
        m_authStep = "success";
        emit isAuthorizedChanged(true);
        emit authStepChanged(m_authStep);
        emit authSuccess("Spotify");
    }
}

void AndroidPlayerBridge::logout(const QString& service) {
    QString svc = service.isEmpty() ? m_currentSource : service;
    m_core.GetRouter().Logout(svc.toStdString());
    m_isAuthorized = false;
    emit isAuthorizedChanged(false);
    m_authStep = "idle";
    m_authStatusMessage = "Вы вышли из учетной записи " + svc;
    emit authStepChanged(m_authStep);
    emit authStatusMessageChanged(m_authStatusMessage);

    if (m_activeTabIndex == 0) {
        m_myTracks.clear();
        m_trackModel.ClearTracks();
    }
    SetStatus(QString("Выход из %1 выполнен").arg(svc));
}

void AndroidPlayerBridge::checkCurrentSourceAuth() {
    if (m_currentSource == "Offline" || m_currentSource.startsWith("Custom:")) {
        if (!m_isAuthorized) {
            m_isAuthorized = true;
            emit isAuthorizedChanged(true);
        }
        return;
    }
    m_core.GetRouter().CheckSourceAuthorized(m_currentSource.toStdString(), [this](bool auth) {
        QMetaObject::invokeMethod(this, [this, auth]() {
            if (m_isAuthorized != auth) {
                m_isAuthorized = auth;
                emit isAuthorizedChanged(auth);
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::pollAudioState() {
    bool playing = m_core.GetAudio().IsPlaying();
    if (m_isPlaying != playing) {
        m_isPlaying = playing;
        emit isPlayingChanged(playing);
    }

    double posSec = m_core.GetAudio().GetPositionSeconds();
    qint64 posMs = static_cast<qint64>(posSec * 1000.0);
    if (std::abs(m_positionMs - posMs) >= 200) {
        m_positionMs = posMs;
        emit positionChanged(m_positionMs);
    }

    double lenSec = m_core.GetAudio().GetLengthSeconds();
    if (lenSec > 0.0) {
        qint64 durMs = static_cast<qint64>(lenSec * 1000.0);
        if (m_durationMs != durMs) {
            m_durationMs = durMs;
            emit durationChanged(m_durationMs);
        }
    }

    bool shuf = m_core.GetPlaylist().IsShuffle();
    if (m_isShuffleCached != shuf) {
        m_isShuffleCached = shuf;
        emit shuffleChanged(shuf);
    }

    int rep = m_core.GetPlaylist().GetRepeatMode();
    if (m_repeatModeCached != rep) {
        m_repeatModeCached = rep;
        emit repeatModeChanged(rep);
    }
}

// =========================================================================
// Crossfade Implementation
// =========================================================================

bool AndroidPlayerBridge::isCrossfade() const {
    if (m_core.GetConfigService()) {
        return m_core.GetConfigService()->GetCrossfadeEnabled();
    }
    return false;
}

void AndroidPlayerBridge::setCrossfade(bool enabled) {
    if (m_core.GetConfigService()) {
        m_core.GetConfigService()->SetCrossfadeEnabled(enabled);
    }
    m_core.GetAudio().SetCrossfadeEnabled(enabled);
    m_core.GetPlaybackCtrl().SetCrossfadeEnabled(enabled);
    Logger::Log(LogLevel::INFO, "AndroidPlayerBridge: setCrossfade -> " + std::string(enabled ? "ON" : "OFF"));
    emit crossfadeChanged(enabled);
}

int AndroidPlayerBridge::crossfadeDuration() const {
    if (m_core.GetConfigService()) {
        return m_core.GetConfigService()->GetCrossfadeDurationMs() / 1000;
    }
    return 3;
}

void AndroidPlayerBridge::setCrossfadeDuration(int seconds) {
    int clamped = std::clamp(seconds, 0, 10);
    int ms = clamped * 1000;
    if (m_core.GetConfigService()) {
        m_core.GetConfigService()->SetCrossfadeDurationMs(ms);
    }
    m_core.GetAudio().SetCrossfadeDurationMs(ms);
    Logger::Log(LogLevel::INFO, "AndroidPlayerBridge: setCrossfadeDuration -> " + std::to_string(clamped) + "s");
    emit crossfadeDurationChanged(clamped);
}

// =========================================================================
// Audio Cache Implementation
// =========================================================================

bool AndroidPlayerBridge::isAudioCacheEnabled() const {
    if (m_core.GetConfigService()) {
        return m_core.GetConfigService()->GetAudioDiskCacheEnabled();
    }
    return false;
}

void AndroidPlayerBridge::setAudioCacheEnabled(bool enabled) {
    if (m_core.GetConfigService()) {
        m_core.GetConfigService()->SetAudioDiskCacheEnabled(enabled);
        emit audioCacheChanged();
    }
}

int AndroidPlayerBridge::audioCacheMaxMb() const {
    if (m_core.GetConfigService()) {
        return m_core.GetConfigService()->GetAudioDiskCacheMaxMb();
    }
    return 300;
}

void AndroidPlayerBridge::setAudioCacheMaxMb(int mb) {
    int clamped = std::clamp(mb, 50, 2000);
    if (m_core.GetConfigService()) {
        m_core.GetConfigService()->SetAudioDiskCacheMaxMb(clamped);
        QThreadPool::globalInstance()->start([clamped]() {
            PathManager::EnforceCacheLimit(PathManager::GetAudioCacheDir(), clamped);
        });
        updateAudioCacheUsedMb();
    }
}

int AndroidPlayerBridge::audioCacheUsedMb() const {
    return m_audioCacheUsedMbCached;
}

void AndroidPlayerBridge::updateAudioCacheUsedMb() {
    QPointer<AndroidPlayerBridge> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis]() {
        if (!safeThis) return;
        QString cacheDir = PathManager::GetAudioCacheDir();
        QDir dir(cacheDir);
        int mb = 0;
        if (dir.exists()) {
            qint64 totalBytes = 0;
            QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& fi : list) {
                if (fi.fileName() == ".nomedia") continue;
                totalBytes += fi.size();
            }
            mb = static_cast<int>(totalBytes / (1024 * 1024));
        }
        QMetaObject::invokeMethod(safeThis, [safeThis, mb]() {
            if (!safeThis) return;
            safeThis->m_audioCacheUsedMbCached = mb;
            emit safeThis->audioCacheChanged();
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::clearAudioCache() {
    QPointer<AndroidPlayerBridge> safeThis(this);
    QThreadPool::globalInstance()->start([safeThis]() {
        QString cacheDir = PathManager::GetAudioCacheDir();
        QDir dir(cacheDir);
        if (dir.exists()) {
            QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& fi : list) {
                if (fi.fileName() == ".nomedia") continue;
                QFile::remove(fi.absoluteFilePath());
            }
        }
        if (safeThis) {
            safeThis->updateAudioCacheUsedMb();
            QMetaObject::invokeMethod(safeThis, [safeThis]() {
                if (safeThis) safeThis->SetStatus("Аудио-кэш очищен");
            }, Qt::QueuedConnection);
        }
    });
}

// =========================================================================
// Shazam Recognition Implementation
// =========================================================================

void AndroidPlayerBridge::startShazamRecognition() {
    if (m_isShazamRecognizing) return;

    m_isShazamRecognizing = true;
    m_shazamResultTitle.clear();
    m_shazamResultArtist.clear();
    m_shazamStatus = "Слушаю аудио (7 секунд)...";
    emit shazamStateChanged();

    QThreadPool::globalInstance()->start([this]() {
        std::string recordPath = m_audioCapture ? m_audioCapture->GetDefaultRecordPath()
                                               : (PathManager::GetTempDir() + "/shazam_mic_record.wav").toStdString();
        bool ok = m_audioCapture ? m_audioCapture->RecordToFile(recordPath, 7) : false;

        if (!ok) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isShazamRecognizing = false;
#ifdef Q_OS_ANDROID
                m_shazamStatus = "Распознавание микрофона на Android в разработке (требуется NDK запись звука). Доступно на Windows.";
#else
                m_shazamStatus = "Ошибка захвата звука с микрофона.";
#endif
                emit shazamStateChanged();
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this]() {
            m_shazamStatus = "Формирование отпечатка (DSP)...";
            emit shazamStateChanged();
        }, Qt::QueuedConnection);

        char* raw_base64 = generate_shazam_signature(recordPath.c_str());
        std::remove(recordPath.c_str());

        if (!raw_base64) {
            QMetaObject::invokeMethod(this, [this]() {
                m_isShazamRecognizing = false;
#ifdef Q_OS_ANDROID
                m_shazamStatus = "На Android библиотека shazamio-core пока недоступна (требуется aarch64 Rust сборка). Доступно на Windows.";
#else
                m_shazamStatus = "Ошибка: не удалось сформировать отпечаток трека.";
#endif
                emit shazamStateChanged();
            }, Qt::QueuedConnection);
            return;
        }

        QString base64Sig = QString::fromUtf8(raw_base64);
        free_shazam_string(raw_base64);

        QMetaObject::invokeMethod(this, [this, base64Sig = std::move(base64Sig)]() {
            m_shazamStatus = "Поиск трека в базе Shazam...";
            emit shazamStateChanged();

            QJsonObject sigObj{ {"uri", base64Sig}, {"samplems", 12000} };
            QJsonObject rootObj{ {"signature", sigObj} };
            QByteArray jsonPayload = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

            QNetworkAccessManager* mgr = m_core.GetNetworkManager();
            if (!mgr) {
                m_isShazamRecognizing = false;
                m_shazamStatus = "Ошибка: сетевой менеджер недоступен.";
                emit shazamStateChanged();
                return;
            }

            QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QUrl url("https://amp.shazam.com/discovery/v5/ru/RU/android/-/tag/" + uuid + "/" + uuid);

            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("User-Agent", "Shazam Android/13.7.0");

            QNetworkReply* reply = mgr->post(request, jsonPayload);
            connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                m_isShazamRecognizing = false;
                if (reply->error() == QNetworkReply::NoError) {
                    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                    QJsonObject trackObj = obj["track"].toObject();
                    if (trackObj.isEmpty()) {
                        m_shazamStatus = "Трек не распознан. Возможно, его нет в базе Shazam.";
                    } else {
                        m_shazamResultTitle = trackObj["title"].toString();
                        m_shazamResultArtist = trackObj["subtitle"].toString();
                        m_shazamStatus = "Успешно распознано!";
                    }
                } else {
                    m_shazamStatus = "Ошибка связи с Shazam: " + reply->errorString();
                }
                reply->deleteLater();
                emit shazamStateChanged();
            });
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::resetShazam() {
    m_isShazamRecognizing = false;
    m_shazamStatus = "Нажмите кнопку для распознавания трека";
    m_shazamResultTitle.clear();
    m_shazamResultArtist.clear();
    emit shazamStateChanged();
}

QString AndroidPlayerBridge::getCoverUrl(const QString& remoteUrl, bool isThumbnail) {
    return services::cache::CoverCacheService::Instance().GetCoverUrl(remoteUrl, isThumbnail);
}

// -----------------------------------------------------------------------------
// Эквалайзер (DSP 10-полосный Biquad)
// -----------------------------------------------------------------------------
bool AndroidPlayerBridge::isEqualizerEnabled() const {
    return m_core.GetAudio().IsEqualizerEnabled();
}

QString AndroidPlayerBridge::equalizerPreset() const {
    return QString::fromStdString(m_core.GetAudio().GetEqualizerPreset());
}

void AndroidPlayerBridge::setEqualizerEnabled(bool enabled) {
    m_core.GetAudio().SetEqualizerEnabled(enabled);
    emit equalizerChanged();
}

void AndroidPlayerBridge::setEqualizerPreset(const QString& preset) {
    m_core.GetAudio().SetEqualizerPreset(preset.toStdString());
    emit equalizerChanged();
}

float AndroidPlayerBridge::getEqualizerBand(int band) const {
    return m_core.GetAudio().GetEqualizerBandGain(band);
}

void AndroidPlayerBridge::setEqualizerBand(int band, float gainDb) {
    m_core.GetAudio().SetEqualizerEnabled(true);
    m_core.GetAudio().SetEqualizerBandGain(band, gainDb);
    m_core.GetAudio().SetEqualizerPreset("Custom");
    emit equalizerChanged();
}

void AndroidPlayerBridge::resetEqualizer() {
    std::vector<float> zeros(10, 0.0f);
    m_core.GetAudio().SetEqualizerBands(zeros);
    m_core.GetAudio().SetEqualizerPreset("Flat");
    emit equalizerChanged();
    SetStatus("Эквалайзер сброшен в 0 dB");
}

QVariantList AndroidPlayerBridge::getEqualizerBands() const {
    QVariantList list;
    auto bands = m_core.GetAudio().GetEqualizerBands();
    for (float b : bands) {
        list.append(b);
    }
    return list;
}

void AndroidPlayerBridge::applyPreset(const QString& presetName) {
    std::string p = presetName.toStdString();
    std::vector<float> gains(10, 0.0f);
    if (presetName == "Bass Boost") {
        gains = {6.0f, 5.0f, 3.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    } else if (presetName == "Rock") {
        gains = {5.0f, 3.0f, 1.0f, -1.0f, -2.0f, 0.0f, 2.0f, 4.0f, 5.0f, 6.0f};
    } else if (presetName == "Vocal Boost") {
        gains = {-2.0f, -1.0f, 0.0f, 2.0f, 4.0f, 4.0f, 2.0f, 1.0f, 0.0f, -1.0f};
    } else if (presetName == "Electronic") {
        gains = {5.0f, 4.0f, 2.0f, 0.0f, -2.0f, 1.0f, 0.0f, 2.0f, 4.0f, 5.0f};
    } else if (presetName == "Classical") {
        gains = {4.0f, 3.0f, 2.0f, 1.0f, -1.0f, -1.0f, 0.0f, 2.0f, 3.0f, 4.0f};
    }
    m_core.GetAudio().SetEqualizerEnabled(true);
    m_core.GetAudio().SetEqualizerBands(gains);
    m_core.GetAudio().SetEqualizerPreset(p);
    emit equalizerChanged();
    SetStatus(QString("Применен пресет эквалайзера: %1").arg(presetName));
}

// -----------------------------------------------------------------------------
// Сортировка и Очередь
// -----------------------------------------------------------------------------
void AndroidPlayerBridge::sortTracks(const QString& mode) {
    m_trackModel.Sort(mode);
    QString label = "По дате добавления";
    if (mode == "date-desc") label = "По дате (новые)";
    else if (mode == "date-asc") label = "По дате (старые)";
    else if (mode == "title-asc") label = "По названию (А → Я)";
    else if (mode == "artist-asc") label = "По исполнителю (А → Я)";
    SetStatus(QString("Список отсортирован: %1").arg(label));
}

void AndroidPlayerBridge::shuffleTracks() {
    m_trackModel.Shuffle();
    if (m_trackModel.rowCount() > 0) {
        playTrackAtIndex(0);
    }
    SetStatus("Список перемешан и запущен");
}

void AndroidPlayerBridge::openFavoritesForSource(const QString& source) {
    QString norm = source.trimmed();
    if (norm.isEmpty()) norm = "All";

    std::vector<Track> tracks;
    if (norm == "All" || norm == "Все" || norm == "Все источники") {
        tracks = m_core.GetDbManager().LoadAllSourcesTracks();
        m_currentSource = "All";
    } else if (norm == "Offline" || norm == "Скачанные") {
        auto allTracks = m_core.GetDbManager().LoadTracks("Offline");
        QSet<QString> downloadedFiles;
        auto scanDir = [&](const QString& dirPath) {
            if (dirPath.isEmpty()) return;
            QDir dir(dirPath);
            if (dir.exists()) {
                const auto entries = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
                for (const auto& f : entries) {
                    downloadedFiles.insert(f.toLower());
                }
            }
        };
        scanDir(PathManager::GetDownloadsDir());
        QString custom = PathManager::GetCustomDownloadsDir();
        if (!custom.isEmpty()) scanDir(custom);

        for (const auto& t : allTracks) {
            QString safeBase = QString::fromStdString(t.GetSafeFilename()).toLower();
            if (downloadedFiles.contains(safeBase + ".mp3") ||
                downloadedFiles.contains(safeBase + ".aac")) {
                tracks.push_back(t);
            }
        }
        m_currentSource = "Offline";
    } else {
        tracks = m_core.GetDbManager().LoadTracks(norm.toStdString());
        m_currentSource = norm;
    }

    for (auto& t : tracks) {
        t.isLibrary = true;
    }

    m_myTracks = tracks;
    m_trackModel.SetTracks(m_myTracks);
    m_core.GetPlaylist().SetTracks(m_myTracks);
    emit currentSourceChanged(m_currentSource);
    SetStatus(QString("Избранное (%1): %2 треков").arg(norm).arg(tracks.size()));
}

void AndroidPlayerBridge::addTrackToQueue(int index) {
    const Track* tPtr = m_trackModel.GetTrackAt(index);
    if (!tPtr) return;
    m_core.GetPlaylist().AddTrack(*tPtr);
    SetStatus(QString("Трек добавлен в очередь: %1").arg(QString::fromStdString(tPtr->title)));
}

bool AndroidPlayerBridge::isTrackInQueue(int index) const {
    const Track* tPtr = m_trackModel.GetTrackAt(index);
    if (!tPtr) return false;
    auto queue = m_core.GetPlaylist().GetQueueTracks();
    for (const auto& t : queue) {
        if (t.id == tPtr->id) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Импорт сетевого плейлиста
// -----------------------------------------------------------------------------
void AndroidPlayerBridge::importNetworkPlaylist(const QString& url) {
    QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;
    SetStatus(QString("Импорт плейлиста: %1...").arg(trimmed));

    QString src = "VK";
    if (trimmed.contains("soundcloud.com", Qt::CaseInsensitive)) src = "SoundCloud";
    else if (trimmed.contains("music.yandex", Qt::CaseInsensitive)) src = "Yandex";
    else if (trimmed.contains("youtube.com", Qt::CaseInsensitive) || trimmed.contains("youtu.be", Qt::CaseInsensitive)) src = "YouTube";

    setSearchSource(src);
    search(trimmed);
}

} // namespace ui::android
