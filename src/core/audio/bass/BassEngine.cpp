#include "BassEngine.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <QFile>
#include <QString>
#include <QFileInfo>
#include <QUrl>

BassEngine::BassEngine() {
}

BassEngine::~BassEngine() {
    if (m_activeStream != 0) {
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncEnd);
        BASS_StreamFree(m_activeStream);
    }
    if (m_fadingStream != 0) {
        BASS_StreamFree(m_fadingStream);
    }
    if (m_hlsPlugin != 0) {
        BASS_PluginFree(m_hlsPlugin);
    }
    BASS_Free();
    Logger::Log(LogLevel::INFO, "bass: BASS freed.");
}

bool BassEngine::Init() {
    QSettings settings("config.ini", QSettings::IniFormat);
    int netTimeout = settings.value("Audio/NetTimeout", 5000).toInt();
    int netReadTimeout = settings.value("Audio/NetReadTimeout", 5000).toInt();
    m_crossfadeDurationMs = settings.value("Audio/CrossfadeDurationMs", 3000).toInt();

    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, netTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_READTIMEOUT, netReadTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 0);
    BASS_SetConfig(BASS_CONFIG_NET_BUFFER, 20000);
    BASS_SetConfig(BASS_CONFIG_NET_PREBUF, 50);
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, "VKAndroidApp/5.56.1-12345 (Android 11; SDK 30; x86_64; en; 2274003)");

    if (!BASS_Init(-1, 44100, 0, 0, nullptr)) {
        Logger::Log(LogLevel::ERROR, "bass: Failed to initialize BASS!");
        return false;
    }

    m_hlsPlugin = BASS_PluginLoad("basshls.dll", 0);
    if (!m_hlsPlugin) {
        Logger::Log(LogLevel::WARNING, "bass: Could not load basshls.dll plugin! Local HLS might fail.");
    }

    return true;
}

void CALLBACK BassEngine::BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user) {
    BassEngine* engine = static_cast<BassEngine*>(user);
    if (engine->OnTrackNearEnd) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
            engine->OnTrackNearEnd();
        }, Qt::QueuedConnection);
    }
}

void CALLBACK BassEngine::BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user) {
    Logger::Log(LogLevel::INFO, "bass: --- TRACK COMPLETION EVENT ---");
    BassEngine* engine = static_cast<BassEngine*>(user);
    if (engine->OnTrackFinished) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
            engine->OnTrackFinished();
        }, Qt::QueuedConnection);
    }
}

bool BassEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    if (m_activeStream != 0) {
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncEnd);
        if (m_syncNearEnd != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncNearEnd);
        if (m_syncCrossfade != 0) BASS_ChannelRemoveSync(m_activeStream, m_syncCrossfade);

        if (crossfade) {
            // Если предыдущий трек еще затухает, убиваем его
            if (m_fadingStream != 0) BASS_StreamFree(m_fadingStream);

            m_fadingStream = m_activeStream;

            // Плавно глушим старый трек за m_crossfadeDurationMs мс
            BASS_ChannelSlideAttribute(m_fadingStream, BASS_ATTRIB_VOL, 0.0f, m_crossfadeDurationMs);

            // Ставим хук: как только громкость упадет до 0, вычищаем трек из памяти
            BASS_ChannelSetSync(m_fadingStream, BASS_SYNC_SLIDE | BASS_SYNC_ONETIME, 0,
                [](HSYNC handle, DWORD channel, DWORD data, void* user) {
                    BASS_StreamFree(channel);
                    Logger::Log(LogLevel::INFO, "bass: Faded track properly freed.");
                }, nullptr);
        } else {
            BASS_StreamFree(m_activeStream);
        }

        m_activeStream = 0;
        m_syncEnd = 0;
        m_syncNearEnd = 0;
    }

    // --- ЛОГИКА ОФФЛАЙН ВОСПРОИЗВЕДЕНИЯ ---
    QString localPath = PathManager::GetDownloadFilePath(trackId, "mp3");
    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(trackId, "aac");
    }

    // играем с диска
    if (!trackId.empty() && QFile::exists(localPath)) {
        Logger::Log(LogLevel::INFO, "bass: Playing from local downloads -> " + localPath.toStdString());
        m_activeStream = BASS_StreamCreateFile(FALSE, localPath.toStdString().c_str(), 0, 0, 0);
    }
    // Иначе из интернета
    else {
        m_activeStream = BASS_StreamCreateURL(url.c_str(), 0, 0, nullptr, nullptr);
    }

    if (m_activeStream != 0) {
        if (crossfade) {
            // Новый трек стартует с 0 громкости и нарастает
            BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, 0.0f);
            BASS_ChannelSlideAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume, m_crossfadeDurationMs);
        } else {
            BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume);
        }

        m_syncEnd = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_END, 0, &BassEngine::BassTrackEndCallback, this);

        QWORD length = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
        double actualDuration = (length != (QWORD)-1) ? BASS_ChannelBytes2Seconds(m_activeStream, length) : static_cast<double>(durationSec);

        double crossfadeSec = m_crossfadeDurationMs / 1000.0;

        if (crossfade && actualDuration > crossfadeSec) {
            QWORD crossfadePos = BASS_ChannelSeconds2Bytes(m_activeStream, actualDuration - crossfadeSec);
            m_syncCrossfade = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_POS | BASS_SYNC_ONETIME, crossfadePos, &BassEngine::BassTrackEndCallback, this);
        }

        // Хук предзагрузки за 10 сек до конца
        if (actualDuration > 10.0) {
            QWORD prefetchPos = BASS_ChannelSeconds2Bytes(m_activeStream, actualDuration - 10.0);
            m_syncNearEnd = BASS_ChannelSetSync(m_activeStream, BASS_SYNC_POS, prefetchPos, &BassEngine::BassTrackNearEndCallback, this);
        }

        BASS_ChannelPlay(m_activeStream, FALSE);
        return true;
    }

    Logger::Log(LogLevel::ERROR, "bass: Failed to create stream! Error: " + std::to_string(BASS_ErrorGetCode()));
    return false;
}

void BassEngine::SetPositionSeconds(double pos) {
    if (m_activeStream != 0) {
        QWORD bytePos = BASS_ChannelSeconds2Bytes(m_activeStream, pos);
        BASS_ChannelSetPosition(m_activeStream, bytePos, BASS_POS_BYTE);

        QWORD length = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
        if (length != (QWORD)-1) {
            QWORD prefetchPos = length - BASS_ChannelSeconds2Bytes(m_activeStream, 10.0);
            if (bytePos >= prefetchPos && OnTrackNearEnd) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                    this->OnTrackNearEnd();
                }, Qt::QueuedConnection);
            }
        }
    }
}

void BassEngine::Pause() {
    if (m_activeStream != 0) BASS_ChannelPause(m_activeStream);
}

void BassEngine::Resume() {
    if (m_activeStream != 0) BASS_ChannelPlay(m_activeStream, FALSE);
}

void BassEngine::SetVolume(float volume) {
    m_volume = volume;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;
    if (m_activeStream != 0) BASS_ChannelSetAttribute(m_activeStream, BASS_ATTRIB_VOL, m_volume);
}

float BassEngine::GetVolume() const { return m_volume; }

bool BassEngine::IsPlaying() const {
    return m_activeStream != 0 && BASS_ChannelIsActive(m_activeStream) == BASS_ACTIVE_PLAYING;
}

double BassEngine::GetPositionSeconds() const {
    if (m_activeStream == 0) return 0.0;
    QWORD pos = BASS_ChannelGetPosition(m_activeStream, BASS_POS_BYTE);
    return (pos == (QWORD)-1) ? 0.0 : BASS_ChannelBytes2Seconds(m_activeStream, pos);
}

double BassEngine::GetLengthSeconds() const {
    if (m_activeStream == 0) return 0.0;
    QWORD len = BASS_ChannelGetLength(m_activeStream, BASS_POS_BYTE);
    return (len == (QWORD)-1) ? 0.0 : BASS_ChannelBytes2Seconds(m_activeStream, len);
}

std::vector<float> BassEngine::GetSpectrumData() {
    std::vector<float> fft(128, 0.0f);
    if (m_activeStream != 0 && BASS_ChannelIsActive(m_activeStream) == BASS_ACTIVE_PLAYING) {
        BASS_ChannelGetData(m_activeStream, fft.data(), BASS_DATA_FFT256);
    }
    return fft;
}