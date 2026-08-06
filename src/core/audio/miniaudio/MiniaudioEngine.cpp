#include "MiniaudioEngine.h"
#include "utils/logger/Logger.h"
#include <cstring>
#include <QFile>
#include <QString>

MiniaudioEngine::MiniaudioEngine() {
    m_isDeviceInitialized = false;
    m_isDecoderInitialized = false;
    m_isPlaying = false;
    m_volume = 1.0f;
}

MiniaudioEngine::~MiniaudioEngine() {
    if (m_isDeviceInitialized) {
        ma_device_uninit(&m_device);
    }
    if (m_isDecoderInitialized) {
        ma_decoder_uninit(&m_decoder);
    }
    if (m_aacDecoder) {
        aacDecoder_Close(m_aacDecoder);
    }
}

void MiniaudioEngine::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    MiniaudioEngine* engine = static_cast<MiniaudioEngine*>(pDevice->pUserData);
    if (!engine) return;

    std::lock_guard<std::mutex> lock(engine->m_audioMutex);

    if (engine->m_isDecoderInitialized && engine->m_isPlaying) {
        ma_uint64 framesRead = 0;
        // Читаем звук из декодера в выходной буфер звуковой карты
        ma_decoder_read_pcm_frames(&engine->m_decoder, pOutput, frameCount, &framesRead);

        if (framesRead < frameCount) {
            ma_uint32 bytesToClear = (frameCount - framesRead) * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
            void* pTail = (ma_uint8*)pOutput + (framesRead * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format));
            std::memset(pTail, 0, bytesToClear);

            // TODO: OnTrackFinished()
        }
    } else {
        ma_uint32 bytesToClear = frameCount * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
        std::memset(pOutput, 0, bytesToClear);
    }
}

bool MiniaudioEngine::Init() {
    // Инициализация RingBuffer (<a> Гц * <b> каналов * <тип данных>) * <c> сек)
    m_pcmBuffer.Init(44100 * 2 * sizeof(float) * 5);
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = 44100;
    deviceConfig.dataCallback      = DataCallback;
    deviceConfig.pUserData         = this;

    m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!m_aacDecoder) {
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to open FDK-AAC decoder.");
        return false;
    }

    if (ma_device_init(NULL, &deviceConfig, &m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to init playback device.");
        return false;
    }

    m_isDeviceInitialized = true;
    Logger::Log(LogLevel::INFO, "Miniaudio: Device initialized successfully.");
    return true;
}

bool MiniaudioEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    std::lock_guard<std::mutex> lock(m_audioMutex);

    if (m_isDecoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_isDecoderInitialized = false;
    }

    QString localPath = QString::fromStdString("downloads/" + trackId + ".mp3");

    if (!trackId.empty() && QFile::exists(localPath)) {
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 44100);

        #ifdef _WIN32
        if (ma_decoder_init_file_w(reinterpret_cast<const wchar_t*>(localPath.utf16()), &decoderConfig, &m_decoder) != MA_SUCCESS) {
        #else
            if (ma_decoder_init_file(localPath.toStdString().c_str(), &decoderConfig, &m_decoder) != MA_SUCCESS) {
        #endif
                Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to init decoder for file: " + localPath.toStdString());
                return false;
            }

        m_isDecoderInitialized = true;
        m_isPlaying = true;

        ma_device_start(&m_device);
        Logger::Log(LogLevel::INFO, "Miniaudio: Playing local file -> " + localPath.toStdString());
        return true;
    }

    Logger::Log(LogLevel::WARNING, "Miniaudio: Network streams not implemented yet. Skipped.");
    return false;
}

void MiniaudioEngine::Pause() {
    if (m_isDeviceInitialized && m_isPlaying) {
        m_isPlaying = false;
        ma_device_stop(&m_device);
    }
}

void MiniaudioEngine::Resume() {
    if (m_isDeviceInitialized && !m_isPlaying && m_isDecoderInitialized) {
        m_isPlaying = true;
        ma_device_start(&m_device);
    }
}

void MiniaudioEngine::SetVolume(float volume) {
    m_volume = volume;
    if (m_volume < 0.0f) m_volume = 0.0f;
    if (m_volume > 1.0f) m_volume = 1.0f;

    if (m_isDeviceInitialized) {
        ma_device_set_master_volume(&m_device, m_volume);
    }
}

float MiniaudioEngine::GetVolume() const { return m_volume; }
bool MiniaudioEngine::IsPlaying() const { return m_isPlaying; }

void MiniaudioEngine::SetPositionSeconds(double pos) {}
double MiniaudioEngine::GetPositionSeconds() const { return 0.0; }
double MiniaudioEngine::GetLengthSeconds() const { return 0.0; }
std::vector<float> MiniaudioEngine::GetSpectrumData() const { return std::vector<float>(128, 0.0f); }