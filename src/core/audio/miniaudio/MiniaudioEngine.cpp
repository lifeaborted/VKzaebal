#include "MiniaudioEngine.h"
#include "utils/logger/Logger.h"
#include <cstring>
#include <QFile>
#include <QString>
#include <algorithm>
#include <chrono>
#include "minimp3.h"

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

            // Логируем не чаще раза в секунду — иначе на каждый device-callback (сотни раз/сек)
            // лог превращается в стену одинаковых строк.
            static auto lastLogTime = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count() >= 1000) {
                lastLogTime = now;
                Logger::Log(LogLevel::INFO, "DataCallback: ring buffer underrun (starving for data)");
            }
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

    mp3dec_init(&m_mp3Decoder);

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
    DecodeAACFrames();
}

void MiniaudioEngine::DecodeAACFrames() {
    std::lock_guard<std::mutex> lock(m_networkMutex);

    size_t bytesConsumed = 0;

    while (m_aacBuffer.size() - bytesConsumed >= 188) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break;
        }

        const uint8_t* tsPacket = m_aacBuffer.data() + bytesConsumed;

        // Быстрый поиск: Если пакет сбит, ищем следующий синхробайт
        if (tsPacket[0] != 0x47) {
            auto startIt = m_aacBuffer.begin() + bytesConsumed;
            auto it = std::find(startIt, m_aacBuffer.end(), 0x47);
            bytesConsumed = std::distance(m_aacBuffer.begin(), it);
            continue;
        }

        uint16_t pid = ((tsPacket[1] & 0x1F) << 8) | tsPacket[2];
        uint8_t pusi = (tsPacket[1] & 0x40) >> 6;
        uint8_t afc  = (tsPacket[3] & 0x30) >> 4;

        size_t payloadOffset = 4;
        if (afc == 2 || afc == 3) {
            uint8_t afLength = tsPacket[4];
            payloadOffset += 1 + afLength;
        }

        if ((afc == 1 || afc == 3) && payloadOffset < 188) {
            size_t payloadSize = 188 - payloadOffset;
            const uint8_t* payload = tsPacket + payloadOffset;

            // Детектим PES-заголовок
            if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t streamId = payload[3];

                // СТРОГАЯ ПРОВЕРКА: Захватываем только аудиопоток (Stream ID от 0xC0 до 0xDF)
                if (m_audioPid == 0x1FFF && streamId >= 0xC0 && streamId <= 0xDF) {
                    m_audioPid = pid;
                    Logger::Log(LogLevel::INFO, "Miniaudio: Locked to AUDIO PID -> " + std::to_string(pid));
                }

                if (pid == m_audioPid) {
                    uint8_t pesHeaderLen = payload[8];
                    size_t pesTotalOffset = 9 + pesHeaderLen;
                    if (pesTotalOffset < payloadSize) {
                        payload += pesTotalOffset;
                        payloadSize -= pesTotalOffset;
                    } else {
                        payloadSize = 0;
                    }

                    // VK мультиплексирует ID3v2-тег прямо в начало audio ES,
                    // перед реальными ADTS-фреймами. Если это начало нового PES-пакета
                    // и он начинается с "ID3" — вычисляем полный размер тега и
                    // выставляем счётчик байт на пропуск (тег может занимать
                    // несколько TS-пакетов подряд).
                    if (payloadSize >= 10 &&
                        payload[0] == 'I' && payload[1] == 'D' && payload[2] == '3') {
                        uint32_t tagSize = ((payload[6] & 0x7F) << 21) |
                                           ((payload[7] & 0x7F) << 14) |
                                           ((payload[8] & 0x7F) << 7)  |
                                            (payload[9] & 0x7F);
                        m_id3BytesToSkip = 10 + tagSize;
                        Logger::Log(LogLevel::INFO, "Found inline ID3 tag in audio ES, skipping " +
                                    std::to_string(m_id3BytesToSkip) + " bytes.");
                    }
                }
            }

            // Пропускаем байты ID3-тега — независимо от того, начало это PES-пакета
            // или его продолжение, т.к. тег может растянуться на несколько TS-пакетов.
            if (pid == m_audioPid && m_id3BytesToSkip > 0 && payloadSize > 0) {
                size_t toSkip = std::min(m_id3BytesToSkip, payloadSize);
                payload += toSkip;
                payloadSize -= toSkip;
                m_id3BytesToSkip -= toSkip;
            }

            // Отправляем в декодер только пакеты захваченного аудио-PID
            if (payloadSize > 0 && pid == m_audioPid) {

                // Определяем формат один раз на трек, по сигнатуре первых байт ES.
                // ADTS(AAC) и MP3 делят один и тот же префикс "FF Ex/Fx" — разделяет их
                // поле layer: у ADTS оно всегда 00, у валидного MP3 — никогда.
                if (m_streamFormat == AudioStreamFormat::Unknown) {
                    m_streamFormat = DetectAudioFormat(payload, payloadSize);
                    if (m_streamFormat != AudioStreamFormat::Unknown) {
                        Logger::Log(LogLevel::INFO, std::string("Miniaudio: Detected stream format -> ") +
                                    (m_streamFormat == AudioStreamFormat::AAC_ADTS ? "AAC (ADTS)" : "MP3"));
                    }
                }

                if (m_streamFormat == AudioStreamFormat::MP3) {
                    DecodeMp3Payload(payload, payloadSize);
                } else {
                    // По умолчанию (в т.ч. пока формат не распознан) пробуем AAC —
                    // так поведение остаётся прежним для сервисов, реально отдающих AAC.
                    DecodeAacPayload(payload, payloadSize);
                }
            }
        }

        bytesConsumed += 188;
    }

    // Очищаем обработанные данные разом
    if (bytesConsumed > 0) {
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + bytesConsumed);
    }
}

MiniaudioEngine::AudioStreamFormat MiniaudioEngine::DetectAudioFormat(const uint8_t* data, size_t size) {
    if (size < 2 || data[0] != 0xFF) {
        return AudioStreamFormat::Unknown;
    }

    uint8_t b1 = data[1];

    // ADTS (AAC): синк 12 бит (4 бита в этом байте) + layer(2 бита) всегда == 00
    if ((b1 & 0xF0) == 0xF0) {
        uint8_t layer = (b1 >> 1) & 0x03;
        if (layer == 0x00) {
            return AudioStreamFormat::AAC_ADTS;
        }
    }

    // MP3: синк 11 бит (3 бита в этом байте) + version != 01(reserved) + layer != 00(reserved)
    if ((b1 & 0xE0) == 0xE0) {
        uint8_t version = (b1 >> 3) & 0x03;
        uint8_t layer = (b1 >> 1) & 0x03;
        if (version != 0x01 && layer != 0x00) {
            return AudioStreamFormat::MP3;
        }
    }

    return AudioStreamFormat::Unknown;
}

void MiniaudioEngine::DecodeAacPayload(const uint8_t* payload, size_t payloadSize) {
    UCHAR* pBuffer = const_cast<UCHAR*>(payload);
    UINT bufferSize = static_cast<UINT>(payloadSize);
    UINT bytesValid = bufferSize;

    aacDecoder_Fill(m_aacDecoder, &pBuffer, &bufferSize, &bytesValid);

    while (true) {
        std::vector<int16_t> pcmBuf(4096);
        AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(m_aacDecoder, pcmBuf.data(), pcmBuf.size(), 0);

        // Данных пока не хватает — это норма, а не ошибка. Ждём следующий TS-пакет.
        if (err == AAC_DEC_NOT_ENOUGH_BITS) {
            break;
        }

        // Настоящая ошибка декодера — логируем и выходим, ждём следующий TS-пакет.
        if (err != AAC_DEC_OK) {
            Logger::Log(LogLevel::WARNING, "AAC decode error: " + std::to_string(err));
            break;
        }

        CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
        if (info && info->sampleRate > 0) {
            size_t bytesToOutput = info->frameSize * info->numChannels * sizeof(int16_t);
            m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
        }
    }
}

void MiniaudioEngine::DecodeMp3Payload(const uint8_t* payload, size_t payloadSize) {
    // minimp3 работает не с "потоком в декодер", а с обычным буфером — накапливаем
    // сырые ES-байты и вырезаем из него по одному фрейму за раз.
    m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + payloadSize);

    // minimp3 требует, чтобы в буфере помещался ПОЛНЫЙ фрейм (для MPEG1 Layer3 на высоких
    // битрейтах это может быть больше 1000 байт), иначе он не может подтвердить синхронизацию
    // и консервативно списывает весь переданный буфер в "мусор" (frame_bytes == размеру буфера,
    // samples == 0, а поля info вообще не валидны). Поэтому не дёргаем декодер на каждый
    // TS-пакет (~184 байта) — ждём, пока накопится запас на несколько фреймов.
    static constexpr size_t kMinBufferForDecode = 8192; // запас на неск. фреймов даже на 320 kbps
    static constexpr size_t kMaxFrameSize = 2048;        // с запасом больше реального макс. фрейма MP3

    if (m_mp3Buffer.size() < kMinBufferForDecode) {
        return; // ждём следующий TS-пакет
    }

    mp3dec_frame_info_t info;
    // Останавливаемся заранее, оставляя "хвост" — иначе последний неполный фрейм в конце
    // буфера будет ошибочно списан как мусор просто потому, что не хватило данных дочитать его.
    while (m_mp3Buffer.size() > kMaxFrameSize) {
        std::vector<int16_t> pcmBuf(MINIMP3_MAX_SAMPLES_PER_FRAME);
        int samples = mp3dec_decode_frame(&m_mp3Decoder, m_mp3Buffer.data(),
                                           static_cast<int>(m_mp3Buffer.size()),
                                           pcmBuf.data(), &info);

        if (info.frame_bytes == 0) {
            // Настоящая нехватка данных — ждём следующий TS-пакет.
            break;
        }

        if (samples > 0 && info.channels > 0) {
            size_t bytesToOutput = static_cast<size_t>(samples) * info.channels * sizeof(int16_t);
            m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
        }

        m_mp3Buffer.erase(m_mp3Buffer.begin(), m_mp3Buffer.begin() + info.frame_bytes);
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
    m_audioPid = 0x1FFF; // Сбрасываем захват PID'а при переключении трека
    m_id3BytesToSkip = 0;
    m_streamFormat = AudioStreamFormat::Unknown;
    m_mp3Buffer.clear();
    mp3dec_init(&m_mp3Decoder); // сбрасываем bit reservoir декодера между треками

    std::lock_guard<std::mutex> netLock(m_networkMutex);
    m_aacBuffer.clear();
}
