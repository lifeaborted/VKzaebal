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

float MiniaudioEngine::GetVolume() const { return m_volume; }
bool MiniaudioEngine::IsPlaying() const { return m_isPlaying; }

void MiniaudioEngine::SetPositionSeconds(double pos) {}
double MiniaudioEngine::GetPositionSeconds() const { return static_cast<double>(m_playbackFrameCount.load()) / 44100.0; }
double MiniaudioEngine::GetLengthSeconds() const { return 0.0; }
std::vector<float> MiniaudioEngine::GetSpectrumData() const { return std::vector<float>(128, 0.0f); }

void MiniaudioEngine::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    MiniaudioEngine* engine = static_cast<MiniaudioEngine*>(pDevice->pUserData);
    if (!engine) return;
    engine->m_playbackFrameCount += frameCount;

    if (engine->m_isDecoderInitialized) {
        // --- 1. локальный файл ---
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&engine->m_decoder, pOutput, frameCount, &framesRead);

        if (framesRead < frameCount) {
            ma_uint32 bytesPerFrame = pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
            void* pTail = static_cast<uint8_t*>(pOutput) + (framesRead * bytesPerFrame);
            std::memset(pTail, 0, (frameCount - framesRead) * bytesPerFrame);
        }
    } else {
        // --- 2. сетевой поток ---
        // Считаем, сколько байт звуковая карта просит прямо сейчас
        ma_uint32 bytesToRead = frameCount * pDevice->playback.channels * ma_get_bytes_per_sample(pDevice->playback.format);
        // Читаем из кольцевого буфера
        size_t bytesRead = engine->m_pcmBuffer.Read(static_cast<uint8_t*>(pOutput), bytesToRead);

        if (bytesRead < bytesToRead) {
            std::memset(static_cast<uint8_t*>(pOutput) + bytesRead, 0, bytesToRead - bytesRead);
        }
        engine->DecodeAACFrames();
    }
}

bool MiniaudioEngine::Init() {
    // Инициализация RingBuffer (<a> Гц * <b> каналов * <тип данных>) * <c> сек)
    m_pcmBuffer.Init(44100 * 2 * sizeof(int16_t) * 5);
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_s16;
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
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, 44100);

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
    if (m_isDeviceInitialized && !m_isPlaying) {
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

void MiniaudioEngine::PushNetworkData(const uint8_t* data, size_t size) {
    {
        std::lock_guard<std::mutex> lock(m_networkMutex);
        m_aacBuffer.insert(m_aacBuffer.end(), data, data + size);
    }
    // Пробуем декодировать то, что пришло
    DecodeAACFrames();
}

void MiniaudioEngine::DecodeAACFrames() {
    std::lock_guard<std::mutex> lock(m_networkMutex);

    while (m_aacBuffer.size() >= 188) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break; // Оставляем сырые данные в m_aacBuffer до тех пор, пока буфер не освободится
        }

        if (m_aacBuffer[0] != 0x47) {
            m_aacBuffer.erase(m_aacBuffer.begin());
            continue;
        }

        // 2. Читаем битовые флаги заголовка TS
        uint8_t pusi = (m_aacBuffer[1] & 0x40) >> 6; // Payload Unit Start Indicator
        uint8_t afc  = (m_aacBuffer[3] & 0x30) >> 4; // Adaptation Field Control

        size_t payloadOffset = 4; // Данные по умолчанию начинаются после 4-го байта

        if (afc == 2 || afc == 3) {
            uint8_t afLength = m_aacBuffer[4];
            payloadOffset += 1 + afLength;
        }

        // 3. Если внутри есть полезная нагрузка
        if ((afc == 1 || afc == 3) && payloadOffset < 188) {
            size_t payloadSize = 188 - payloadOffset;
            const uint8_t* payload = m_aacBuffer.data() + payloadOffset;

            // 4. Если PUSI == 1, значит начинается PES-контейнер
            if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t pesHeaderLen = payload[8]; // Длина заголовка PES
                size_t pesTotalOffset = 9 + pesHeaderLen;

                if (pesTotalOffset < payloadSize) {
                    payload += pesTotalOffset;       // Сдвигаем указатель к чистым аудиоданным
                    payloadSize -= pesTotalOffset;
                } else {
                    payloadSize = 0;
                }
            }

            // 5. передаем читстый aac в декодер FDK
            if (payloadSize > 0) {
                UCHAR* pBuffer = const_cast<UCHAR*>(payload);
                UINT bufferSize = static_cast<UINT>(payloadSize);
                UINT bytesValid = bufferSize;

                // Загружаем байты в память кодека
                aacDecoder_Fill(m_aacDecoder, &pBuffer, &bufferSize, &bytesValid);

                // Декодируем все в чанке
                while (true) {
                    // Создаем буфер под сырой PCM
                    std::vector<int16_t> pcmBuf(4096);

                    AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(m_aacDecoder, pcmBuf.data(), pcmBuf.size(), 0);

                    if (err == AAC_DEC_NOT_ENOUGH_BITS) {
                        break; // Кодеку не хватает байтов для целого кадра, ждем следующий TS-пакет
                    }
                    if (err != AAC_DEC_OK) {
                        break; // Попался битый кадр, игнорируем
                    }

                    // узнаем размер декодированного кадра
                    CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
                    if (info && info->sampleRate > 0) {
                        // Считаем размер в байтах: кол-во сэмплов * каналы * размер int16_t
                        size_t bytesToOutput = info->frameSize * info->numChannels * sizeof(int16_t);

                        // передаем готовый звук в потокобезопасный кольцевой буфер
                        m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
                    }
                }
            }
        }

        // Выкидываем обработанный кусок и идем к следующему
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + 188);
    }
}

void MiniaudioEngine::ClearBuffers() {
    std::lock_guard<std::mutex> lock(m_audioMutex);

    if (m_isDecoderInitialized) {
        ma_decoder_uninit(&m_decoder);
        m_isDecoderInitialized = false;
    }

    m_pcmBuffer.Clear();
    m_playbackFrameCount = 0;

    std::lock_guard<std::mutex> netLock(m_networkMutex);
    m_aacBuffer.clear();
}