#include "AndroidPlayerBridge.h"
#include "TrackListModel.h"
#include "core/ApplicationCore.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/audio/playback/PlaybackController.h"
#include "core/api/vk/VkAuthService.h"
#include "utils/logger/Logger.h"

#include <algorithm>
#include <QMetaObject>

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
    return {"vk", "youtube", "soundcloud", "yandex", "spotify"};
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
    m_currentCoverUrl = QString::fromStdString(track.coverUrl);
    m_durationMs = static_cast<qint64>(track.duration) * 1000;

    emit currentTrackChanged();
    emit durationChanged(m_durationMs);

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

void AndroidPlayerBridge::OnAudioFetched(const std::vector<Track>& tracks) {
    if (m_activeTabIndex == 0) {
        m_myTracks = tracks;
        m_trackModel.SetTracks(tracks);
    }
}

void AndroidPlayerBridge::OnFinishedFetching() {
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
    SetStatus(QString("Поиск \"%1\" в %2...").arg(q, m_currentSource));

    m_activeTabIndex = 1; // Переключаемся на вкладку результатов поиска
    emit activeTabIndexChanged(1);

    std::string src = m_currentSource.toStdString();
    std::string queryString = q.toStdString();

    m_core.GetRouter().Search(src, queryString, 50, 0, [this, q](const std::vector<Track>& tracks, const std::string& error) {
        QMetaObject::invokeMethod(this, [this, tracks, error, q]() {
            m_isSearching = false;
            emit isSearchingChanged(false);

            if (!error.empty()) {
                SetStatus(QString("Ошибка поиска: %1").arg(QString::fromStdString(error)));
            } else {
                SetStatus(QString("Найдено %1 треков").arg(tracks.size()));
                m_trackModel.SetTracks(tracks);
            }
        }, Qt::QueuedConnection);
    });
}

void AndroidPlayerBridge::playTrackAtIndex(int index) {
    const Track* track = m_trackModel.GetTrackAt(index);
    if (track) {
        m_trackModel.SetPlayingIndex(index);
        m_core.GetPlaylist().PlayTrackNow(*track);
    }
}

void AndroidPlayerBridge::switchSource(const QString& sourceName) {
    QString s = sourceName.toLower();
    if (m_currentSource != s) {
        m_currentSource = s;
        emit currentSourceChanged(m_currentSource);
        SetStatus(QString("Переключение на источник: %1").arg(s));
        m_core.GetRouter().SwitchSource(s.toStdString());
    }
}

void AndroidPlayerBridge::loadMyTracks() {
    setActiveTabIndex(0);
    SetStatus("Загрузка плейлиста...");
    m_core.GetRouter().SwitchSource(m_currentSource.toStdString());
}

void AndroidPlayerBridge::loginVk(const QString& login, const QString& password, const QString& code2fa) {
    VkAuthService* auth = m_core.GetRouter().GetVkAuthService();
    if (!auth) {
        SetStatus("VK авторизация недоступна");
        return;
    }
    SetStatus("Выполняется нативный вход VK...");
    auth->StartLogin(login, password);
    if (!code2fa.isEmpty()) {
        auth->SubmitCode(code2fa);
    }
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
}

} // namespace ui::android
