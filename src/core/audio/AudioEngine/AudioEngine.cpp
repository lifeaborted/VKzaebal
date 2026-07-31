#include "AudioEngine.h"
#include "utils/logger/logger.h"

AudioEngine::AudioEngine() : m_isInitialized(false), m_isPlaying(false) {
    Logger::Log(LogLevel::INFO, "AudioEngine created.");
}

AudioEngine::~AudioEngine() {
    Shutdown();
    Logger::Log(LogLevel::INFO, "AudioEngine destroyed.");
}

bool AudioEngine::Initialize(const std::string& filePath) {
    Logger::Log(LogLevel::INFO, "Initializing AudioEngine with file: " + filePath);

    if (ma_decoder_init_file(filePath.c_str(), NULL, &m_decoder) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to initialize decoder. Check file path.");
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = m_decoder.outputFormat;
    deviceConfig.playback.channels = m_decoder.outputChannels;
    deviceConfig.sampleRate        = m_decoder.outputSampleRate;
    deviceConfig.dataCallback      = DataCallback;
    deviceConfig.pUserData         = this; // Передаем указатель на текущий экземпляр класса

    if (ma_device_init(NULL, &deviceConfig, &m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to initialize playback device.");
        ma_decoder_uninit(&m_decoder);
        return false;
    }

    m_isInitialized = true;
    Logger::Log(LogLevel::INFO, "AudioEngine initialized successfully.");
    return true;
}

void AudioEngine::Play() {
    if (!m_isInitialized) return;
    
    if (ma_device_start(&m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to start device playback.");
        return;
    }
    m_isPlaying = true;
    Logger::Log(LogLevel::INFO, "Playback started/resumed.");
}

void AudioEngine::Pause() {
    if (!m_isInitialized || !m_isPlaying) return;

    if (ma_device_stop(&m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to stop device playback.");
        return;
    }
    m_isPlaying = false;
    Logger::Log(LogLevel::INFO, "Playback paused.");
}

void AudioEngine::SetVolume(float volume) {
    if (!m_isInitialized) return;
    
    // Защита от выхода за пределы
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    ma_device_set_master_volume(&m_device, volume);
    Logger::Log(LogLevel::INFO, "Volume set to: " + std::to_string(volume));
}

void AudioEngine::Shutdown() {
    if (m_isInitialized) {
        Logger::Log(LogLevel::INFO, "Shutting down AudioEngine...");
        ma_device_uninit(&m_device);
        ma_decoder_uninit(&m_decoder);
        m_isInitialized = false;
        m_isPlaying = false;
        Logger::Log(LogLevel::INFO, "AudioEngine shutdown complete.");
    }
}

void AudioEngine::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    // Получаем объект обратно из pUserData
    AudioEngine* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (engine && engine->m_isInitialized) {
        ma_decoder_read_pcm_frames(&engine->m_decoder, pOutput, frameCount, NULL);
    }
    (void)pInput;
}

ma_result AudioEngine::CustomRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead) {
    auto* engine = static_cast<AudioEngine*>(pDecoder->pUserData);

    if (!engine || !engine->m_ringBuffer) {
        if (pBytesRead) *pBytesRead = 0;
        return MA_ERROR;
    }

    // Читаем байты напрямую из RingBuffer в буфер miniaudio
    size_t actualRead = engine->m_ringBuffer->Read(static_cast<uint8_t*>(pBufferOut), bytesToRead);

    if (pBytesRead) {
        *pBytesRead = actualRead;
    }

    return MA_SUCCESS;
}

ma_result AudioEngine::CustomSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin) {
    auto* engine = static_cast<AudioEngine*>(pDecoder->pUserData);
    if (!engine || !engine->m_ringBuffer) {
        return MA_ERROR;
    }

    if (origin == ma_seek_origin_current && byteOffset > 0) {
        std::vector<uint8_t> dummy(static_cast<size_t>(byteOffset));
        engine->m_ringBuffer->Read(dummy.data(), dummy.size());
        return MA_SUCCESS;
    }

    // Произвольная перемотка назад пока не поддерживается
    return MA_NOT_IMPLEMENTED;
}

bool AudioEngine::InitializeStream(size_t bufferSize) {
    // Выделяем память под кольцевой буфер (по умолчанию 2 МБ)
    m_ringBuffer = std::make_unique<RingBuffer>(bufferSize);

    m_isDecoderReady = false;
    m_isInitialized = true;

    Logger::Log(LogLevel::INFO, "AudioEngine stream initialized with buffer size: " + std::to_string(bufferSize) + " bytes.");
    return true;
}

void AudioEngine::PushAudioData(const uint8_t* data, size_t size) {
    if (!m_ringBuffer) return;

    // Пишем данные в кольцевой буфер
    size_t written = m_ringBuffer->Write(data, size);

    if (written < size) {
        Logger::Log(LogLevel::WARNING, "RingBuffer overflow! Lost " + std::to_string(size - written) + " bytes.");
    }

    // Если декодер еще не запущен, пытаемся его стартовать
    if (!m_isDecoderReady) {
        if (TryStartDecoder()) {
            Play(); // Автоматически начинаем воспроизведение, как только накопили пре-буфер!
        }
    }
}

bool AudioEngine::TryStartDecoder() {
    if (m_isDecoderReady) return true;

    // Снижаем порог до 32 КБ, чтобы декодер мгновенно подхватывал поток
    const size_t PREBUFFER_SIZE = 32 * 1024;

    if (m_ringBuffer->GetAvailableRead() < PREBUFFER_SIZE) {
        return false;
    }

    ma_decoder_config config = ma_decoder_config_init_default();
    config.encodingFormat = ma_encoding_format_mp3;

    ma_result result = ma_decoder_init(CustomRead, CustomSeek, this, &config, &m_decoder);
    if (result != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to initialize custom decoder from memory.");
        return false;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = m_decoder.outputFormat;
    deviceConfig.playback.channels = m_decoder.outputChannels;
    deviceConfig.sampleRate        = m_decoder.outputSampleRate;
    deviceConfig.dataCallback      = DataCallback;
    deviceConfig.pUserData         = &m_decoder;

    if (ma_device_init(NULL, &deviceConfig, &m_device) != MA_SUCCESS) {
        Logger::Log(LogLevel::ERROR, "Failed to initialize playback device.");
        ma_decoder_uninit(&m_decoder);
        return false;
    }

    m_isDecoderReady = true;
    Logger::Log(LogLevel::INFO, "Decoder initialized from memory buffer successfully! Ready to play.");
    return true;
}