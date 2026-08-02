#include "AudioEngine.h"
#include "utils/logger/logger.h"
#include <QCoreApplication>
#include <QMetaObject>

AudioEngine::AudioEngine() {
}

AudioEngine::~AudioEngine() {
    if (m_currentStream != 0) {
        BASS_StreamFree(m_currentStream);
    }
    BASS_Free();
    Logger::Log(LogLevel::INFO, "AudioEngine: BASS freed.");
}

bool AudioEngine::Init() {
    if (!BASS_Init(-1, 44100, 0, 0, nullptr)) {
        Logger::Log(LogLevel::ERROR, "AudioEngine: Failed to initialize BASS. Code: " + std::to_string(BASS_ErrorGetCode()));
        return false;
    }

    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, 15000);
    BASS_SetConfig(BASS_CONFIG_NET_READTIMEOUT, 15000);
    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 0);
    BASS_SetConfig(BASS_CONFIG_NET_BUFFER, 15000); // 15 секунд сетевого буфера
    BASS_SetConfig(BASS_CONFIG_NET_PREBUF, 50);    // Стартовать воспроизведение только после заполнения буфера на 50%
    BASS_SetConfigPtr(BASS_CONFIG_NET_AGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    HPLUGIN hlsPlugin = BASS_PluginLoad("basshls.dll", 0);

    if (hlsPlugin == 0) {
        Logger::Log(LogLevel::ERROR, "AudioEngine: Failed to load basshls.dll!");
        return false;
    }

    Logger::Log(LogLevel::INFO, "AudioEngine: BASS & HLS Plugin initialized successfully.");
    return true;
}

bool AudioEngine::PlayStream(const std::string& url) {
    if (m_currentStream != 0) {
        BASS_StreamFree(m_currentStream);
        m_currentStream = 0;
    }

    m_currentStream = BASS_StreamCreateURL(url.c_str(), 0, BASS_STREAM_AUTOFREE, nullptr, nullptr);

    if (m_currentStream != 0) {
        BASS_ChannelSetAttribute(m_currentStream, BASS_ATTRIB_VOL, m_volume);
        BASS_ChannelSetSync(m_currentStream, BASS_SYNC_END, 0, &AudioEngine::BassTrackEndCallback, this);
        BASS_ChannelPlay(m_currentStream, FALSE);
        return true;
    } else {
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