#include "AudioEngine.h"
#include "utils/logger/logger.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>

AudioEngine::AudioEngine() {
}

AudioEngine::~AudioEngine() {
    if (m_currentStream != 0) {
        // Снимаем коллбэк перед удалением потока, чтобы избежать гонки потоков
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_currentStream, m_syncEnd);
        BASS_StreamFree(m_currentStream);
    }

    // Освобождаем плагин (закрываем утечку памяти)
    if (m_hlsPlugin != 0) {
        BASS_PluginFree(m_hlsPlugin);
    }

    BASS_Free();
    Logger::Log(LogLevel::INFO, "AudioEngine: BASS freed.");
}

bool AudioEngine::Init() {
    // Читаем настройки из config.ini
    QSettings settings("config.ini", QSettings::IniFormat);

    int netTimeout = settings.value("Audio/NetTimeout", 15000).toInt();
    int netReadTimeout = settings.value("Audio/NetReadTimeout", 15000).toInt();
    int netBuffer = settings.value("Audio/NetBuffer", 20000).toInt();
    int netPrebuf = settings.value("Audio/NetPrebuf", 50).toInt();

    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, netTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_READTIMEOUT, netReadTimeout);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 0);
    BASS_SetConfig(BASS_CONFIG_NET_BUFFER, netBuffer);
    BASS_SetConfig(BASS_CONFIG_NET_PREBUF, netPrebuf);
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    if (!BASS_Init(-1, 44100, 0, 0, nullptr)) {
        Logger::Log(LogLevel::ERROR, "AudioEngine: Failed to initialize BASS. Code: " + std::to_string(BASS_ErrorGetCode()));
        return false;
    }

    m_hlsPlugin = BASS_PluginLoad("basshls.dll", 0);
    if (m_hlsPlugin == 0) {
        Logger::Log(LogLevel::ERROR, "AudioEngine: Failed to load basshls.dll!");
        return false;
    }

    Logger::Log(LogLevel::INFO, "AudioEngine: BASS & HLS Plugin initialized successfully.");
    return true;
}

bool AudioEngine::PlayStream(const std::string& url) {
    if (m_currentStream != 0) {
        if (m_syncEnd != 0) BASS_ChannelRemoveSync(m_currentStream, m_syncEnd);
        BASS_StreamFree(m_currentStream);
        m_currentStream = 0;
        m_syncEnd = 0;
    }

    m_currentStream = BASS_StreamCreateURL(url.c_str(), 0, 0, nullptr, nullptr);
    if (m_currentStream != 0) {
        BASS_ChannelSetAttribute(m_currentStream, BASS_ATTRIB_VOL, m_volume);
        // Запоминаем хэндл коллбэка
        m_syncEnd = BASS_ChannelSetSync(m_currentStream, BASS_SYNC_END, 0, &AudioEngine::BassTrackEndCallback, this);
        BASS_ChannelPlay(m_currentStream, FALSE);
        return true;
    } else {
        // Жестко обнуляем стрим при ошибке, чтобы IsPlaying() отдавал false
        m_currentStream = 0;
        m_syncEnd = 0;
        Logger::Log(LogLevel::ERROR, "AudioEngine: Failed to create stream! Error code: " + std::to_string(BASS_ErrorGetCode()));
        return false;
    }
}

void AudioEngine::Pause() {
    if (m_currentStream != 0 && BASS_ChannelIsActive(m_currentStream) == BASS_ACTIVE_PLAYING) {
        BASS_ChannelPause(m_currentStream);
        Logger::Log(LogLevel::INFO, "AudioEngine: Paused.");
    }
}

void AudioEngine::Resume() {
    if (m_currentStream != 0 && BASS_ChannelIsActive(m_currentStream) == BASS_ACTIVE_PAUSED) {
        BASS_ChannelPlay(m_currentStream, FALSE);
        Logger::Log(LogLevel::INFO, "AudioEngine: Resumed.");
    }
}

void AudioEngine::SetVolume(float volume) {
    m_volume = volume;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;
    if (m_currentStream != 0) {
        BASS_ChannelSetAttribute(m_currentStream, BASS_ATTRIB_VOL, m_volume);
    }
    Logger::Log(LogLevel::INFO, "AudioEngine: Volume set to " + std::to_string(static_cast<int>(m_volume * 100)) + "%");
}

float AudioEngine::GetVolume() const {
    return m_volume;
}

bool AudioEngine::IsPlaying() const {
    return m_currentStream != 0 && BASS_ChannelIsActive(m_currentStream) == BASS_ACTIVE_PLAYING;
}

void CALLBACK AudioEngine::BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user) {
    Logger::Log(LogLevel::INFO, "AudioEngine: --- TRACK COMPLETION EVENT ---");
    AudioEngine* engine = static_cast<AudioEngine*>(user);
    if (engine->OnTrackFinished) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
            engine->OnTrackFinished();
        }, Qt::QueuedConnection);
    }
}

double AudioEngine::GetPositionSeconds() const {
    if (m_currentStream == 0) return 0.0;
    QWORD pos = BASS_ChannelGetPosition(m_currentStream, BASS_POS_BYTE);
    if (pos == (QWORD)-1) return 0.0;
    return BASS_ChannelBytes2Seconds(m_currentStream, pos);
}

double AudioEngine::GetLengthSeconds() const {
    if (m_currentStream == 0) return 0.0;
    QWORD len = BASS_ChannelGetLength(m_currentStream, BASS_POS_BYTE);
    if (len == (QWORD)-1) return 0.0;
    return BASS_ChannelBytes2Seconds(m_currentStream, len);
}