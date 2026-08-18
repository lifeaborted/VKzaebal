#include "MiniaudioEngine.h"
#include "utils/logger/Logger.h"
#include "minimp3.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QString>
#include <algorithm>
#include <chrono>
#include <QCoreApplication>
#include <QSettings>
#include <complex>
#include <cmath>

const double PI = 3.14159265358979323846;
typedef std::complex<double> Complex;

// Быстрое преобразование Фурье (FFT) Кули-Тьюки
static void SimpleFFT(std::vector<Complex>& a) {
    size_t n = a.size();
    if (n <= 1) return;
    std::vector<Complex> a0(n / 2), a1(n / 2);
    for (size_t i = 0; i < n / 2; i++) {
        a0[i] = a[i * 2];
        a1[i] = a[i * 2 + 1];
    }
    SimpleFFT(a0);
    SimpleFFT(a1);
    double ang = 2 * PI / n;
    Complex w(1), wn(cos(ang), sin(ang));
    for (size_t i = 0; i < n / 2; i++) {
        a[i] = a0[i] + w * a1[i];
        a[i + n / 2] = a0[i] - w * a1[i];
        w *= wn;
    }
}

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
    StopFadeOut();
    m_decoder.reset();
    if (m_aacDecoder) {
        aacDecoder_Close(m_aacDecoder);
    }
}

float MiniaudioEngine::GetVolume() const { return m_volume; }

bool MiniaudioEngine::IsPlaying() const { return m_isPlaying; }

void MiniaudioEngine::SetPositionSeconds(double pos) {
    if (m_currentDurationSec <= 0) return;

    if (pos < 0.0) pos = 0.0;
    if (pos > static_cast<double>(m_currentDurationSec)) pos = static_cast<double>(m_currentDurationSec);

    std::lock_guard<std::mutex> lock(m_audioMutex);

    if (m_decoder) {
        ma_uint64 targetFrame = static_cast<ma_uint64>(pos * 44100.0);
        // ИСПРАВЛЕНО: добавлено .get()
        if (ma_decoder_seek_to_pcm_frame(m_decoder.get(), targetFrame) == MA_SUCCESS) {
            m_playbackFrameCount = targetFrame;
            m_nearEndTriggered = false;
            m_finishedTriggered = false;
            Logger::Log(LogLevel::INFO, "Miniaudio: Seeked to " + std::to_string(pos) + "s");
        }
    } else {
        m_playbackFrameCount = static_cast<ma_uint64>(pos * 44100.0);
        m_nearEndTriggered = false;
        m_finishedTriggered = false;

        m_pcmBuffer.Clear();
        {
            std::lock_guard<std::mutex> netLock(m_networkMutex);
            m_aacBuffer.clear();
        }
        m_mp3Buffer.clear();

        m_audioPid = 0x1FFF;
        m_id3BytesToSkip = 0;
        m_streamFormat = AudioStreamFormat::Unknown;

        if (m_aacDecoder) {
            aacDecoder_Close(m_aacDecoder);
            m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
        }
        mp3dec_init(&m_mp3Decoder);

        if (OnNetworkSeekRequested) {
            OnNetworkSeekRequested(pos);
        }
    }
}

double MiniaudioEngine::GetPositionSeconds() const { return static_cast<double>(m_playbackFrameCount.load()) / 44100.0; }

double MiniaudioEngine::GetLengthSeconds() const {
    return static_cast<double>(m_currentDurationSec);
}

std::vector<float> MiniaudioEngine::GetSpectrumData() const {
    std::vector<float> result(128, 0.0f);
    if (!m_isPlaying) return result;

    std::vector<Complex> a(256);
    {
        std::lock_guard<std::mutex> lock(m_spectrumMutex);
        for (int i = 0; i < 256; ++i) {
            double multiplier = 0.5 * (1.0 - cos(2 * PI * i / 255.0));
            a[i] = Complex(m_recentSamples[i] * multiplier, 0);
        }
    }

    SimpleFFT(a);

    for (int i = 0; i < 128; ++i) {
        float mag = static_cast<float>(std::abs(a[i]) / 128.0) * 2.5f;
        result[i] = mag;
    }

    return result;
}

bool MiniaudioEngine::Init() {
    QSettings settings("config.ini", QSettings::IniFormat);
    m_crossfadeDurationMs = settings.value("Audio/CrossfadeDurationMs", 3000).toInt();

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

void MiniaudioEngine::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    MiniaudioEngine* engine = static_cast<MiniaudioEngine*>(pDevice->pUserData);
    if (!engine) return;

    {
        std::lock_guard<std::mutex> lock(engine->m_audioMutex);
        engine->m_playbackFrameCount += frameCount;

        std::vector<int16_t> mainBuffer(frameCount * 2, 0);
        ma_uint32 framesRead = 0;

        if (engine->m_decoder) {
            ma_uint64 read = 0;
            // ИСПРАВЛЕНО: добавлено .get()
            ma_decoder_read_pcm_frames(engine->m_decoder.get(), mainBuffer.data(), frameCount, &read);
            framesRead = read;
        } else {
            ma_uint32 bytesToRead = frameCount * 2 * sizeof(int16_t);
            size_t bytesRead = engine->m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(mainBuffer.data()), bytesToRead);
            framesRead = bytesRead / (2 * sizeof(int16_t));
        }

        std::vector<int16_t> fadeOutBuffer(frameCount * 2, 0);
        ma_uint32 fadeOutFramesRead = 0;

        if (engine->m_isCrossfading) {
            if (engine->m_fadeOutIsLocal && engine->m_fadeOutDecoder) {
                ma_uint64 read = 0;
                // ИСПРАВЛЕНО: добавлено .get()
                ma_decoder_read_pcm_frames(engine->m_fadeOutDecoder.get(), fadeOutBuffer.data(), frameCount, &read);
                fadeOutFramesRead = read;
            } else {
                size_t elementsAvail = engine->m_fadeOutPcm.size() - engine->m_fadeOutPcmReadPos;
                size_t elementsToRead = frameCount * 2;
                if (elementsToRead > elementsAvail) elementsToRead = elementsAvail;

                if (elementsToRead > 0) {
                    std::memcpy(fadeOutBuffer.data(), engine->m_fadeOutPcm.data() + engine->m_fadeOutPcmReadPos, elementsToRead * sizeof(int16_t));
                    engine->m_fadeOutPcmReadPos += elementsToRead;
                    fadeOutFramesRead = elementsToRead / 2;
                }
            }
        }

        int16_t* pOut = static_cast<int16_t*>(pOutput);
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float mainVol = 1.0f;
            float fadeOutVol = 0.0f;

            if (engine->m_isCrossfading) {
                if (engine->m_crossfadeFramesRemaining > 0) {
                    float progress = 1.0f - (static_cast<float>(engine->m_crossfadeFramesRemaining) / engine->m_crossfadeFramesTotal);
                    mainVol = progress;
                    fadeOutVol = 1.0f - progress;
                    engine->m_crossfadeFramesRemaining--;
                } else {
                    engine->StopFadeOut();
                }
            }

            for (int c = 0; c < 2; ++c) {
                int idx = i * 2 + c;
                float s1 = (i < framesRead) ? mainBuffer[idx] * mainVol : 0.0f;
                float s2 = (i < fadeOutFramesRead) ? fadeOutBuffer[idx] * fadeOutVol : 0.0f;

                float mixed = s1 + s2;
                if (mixed > 32767.0f) mixed = 32767.0f;
                if (mixed < -32768.0f) mixed = -32768.0f;

                pOut[idx] = static_cast<int16_t>(mixed);
            }
        }

        {
            std::lock_guard<std::mutex> specLock(engine->m_spectrumMutex);
            size_t samplesToCopy = std::min(static_cast<size_t>(frameCount), static_cast<size_t>(256));

            if (samplesToCopy < 256) {
                std::memmove(engine->m_recentSamples.data(),
                             engine->m_recentSamples.data() + samplesToCopy,
                             (256 - samplesToCopy) * sizeof(float));
            }

            size_t startIdx = 256 - samplesToCopy;
            size_t pOutStart = frameCount - samplesToCopy;
            for (size_t i = 0; i < samplesToCopy; ++i) {
                float left = pOut[(pOutStart + i) * 2] / 32768.0f;
                float right = pOut[(pOutStart + i) * 2 + 1] / 32768.0f;
                engine->m_recentSamples[startIdx + i] = (left + right) / 2.0f;
            }
        }

        double currentSec = static_cast<double>(engine->m_playbackFrameCount.load()) / 44100.0;
        double totalSec = static_cast<double>(engine->m_currentDurationSec);
        double crossfadeSec = engine->m_crossfadeDurationMs / 1000.0;

        if (totalSec > 0.0) {
            if (!engine->m_nearEndTriggered && currentSec >= totalSec - 10.0) {
                engine->m_nearEndTriggered = true;
                if (engine->OnTrackNearEnd) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
                        engine->OnTrackNearEnd();
                    }, Qt::QueuedConnection);
                }
            }

            double endTriggerSec = totalSec;
            if (engine->m_crossfadeDurationMs > 0) {
                endTriggerSec = totalSec - crossfadeSec;
            }

            if (!engine->m_finishedTriggered && currentSec >= endTriggerSec) {
                engine->m_finishedTriggered = true;
                if (engine->OnTrackFinished) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
                        engine->OnTrackFinished();
                    }, Qt::QueuedConnection);
                }
            }
        }
    }

    engine->DecodeAACFrames();
}

bool MiniaudioEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    m_currentTrackId = trackId;

    QString localPath = PathManager::GetDownloadFilePath(trackId, "mp3");

    if (!QFile::exists(localPath)) {
        localPath = PathManager::GetDownloadFilePath(trackId, "aac");
    }

    if (trackId.empty() || !QFile::exists(localPath)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_audioMutex);

    m_currentDurationSec = durationSec;
    m_nearEndTriggered = false;
    m_finishedTriggered = false;
    m_playbackFrameCount = 0;

    if (crossfade && m_crossfadeDurationMs > 0) {
        InitiateCrossfade();
    } else {
        StopFadeOut();
        m_decoder.reset();
        m_pcmBuffer.Clear();
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, 44100);
    m_decoder.reset(new ma_decoder);

#ifdef _WIN32
    // ИСПРАВЛЕНО: Используем правильную функцию для Win32 и добавляем .get()
    if (ma_decoder_init_file_w(reinterpret_cast<const wchar_t*>(localPath.utf16()), &decoderConfig, m_decoder.get()) != MA_SUCCESS) {
#else
    if (ma_decoder_init_file(localPath.toStdString().c_str(), &decoderConfig, m_decoder.get()) != MA_SUCCESS) {
#endif
        m_decoder.reset();
        Logger::Log(LogLevel::ERROR, "Miniaudio: Failed to init decoder for file: " + localPath.toStdString());
        return false;
    }

    m_isPlaying = true;
    ma_device_start(&m_device);
    Logger::Log(LogLevel::INFO, "Miniaudio: Playing local file -> " + localPath.toStdString());
    return true;
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
        float actualVolume = std::pow(m_volume, 3.0f);
        ma_device_set_master_volume(&m_device, actualVolume);
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

            if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t streamId = payload[3];

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

            if (pid == m_audioPid && m_id3BytesToSkip > 0 && payloadSize > 0) {
                size_t toSkip = std::min(m_id3BytesToSkip, payloadSize);
                payload += toSkip;
                payloadSize -= toSkip;
                m_id3BytesToSkip -= toSkip;
            }

            if (payloadSize > 0 && pid == m_audioPid) {

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
                    DecodeAacPayload(payload, payloadSize);
                }
            }
        }

        bytesConsumed += 188;
    }

    if (bytesConsumed > 0) {
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + bytesConsumed);
    }
}

MiniaudioEngine::AudioStreamFormat MiniaudioEngine::DetectAudioFormat(const uint8_t* data, size_t size) {
    if (size < 2 || data[0] != 0xFF) {
        return AudioStreamFormat::Unknown;
    }

    uint8_t b1 = data[1];

    if ((b1 & 0xF0) == 0xF0) {
        uint8_t layer = (b1 >> 1) & 0x03;
        if (layer == 0x00) {
            return AudioStreamFormat::AAC_ADTS;
        }
    }

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

        if (err == AAC_DEC_NOT_ENOUGH_BITS) break;
        if (err != AAC_DEC_OK) {
            Logger::Log(LogLevel::WARNING, "AAC decode error: " + std::to_string(err));
            break;
        }

        CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
        if (info && info->numChannels > 0) {
            if (info->numChannels == 1) {
                std::vector<int16_t> stereoBuf(info->frameSize * 2);
                for (int i = 0; i < info->frameSize; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                size_t bytesToOutput = info->frameSize * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(stereoBuf.data()), bytesToOutput);
            } else {
                size_t bytesToOutput = info->frameSize * info->numChannels * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
            }
        }
    }
}

void MiniaudioEngine::DecodeMp3Payload(const uint8_t* payload, size_t payloadSize) {
    m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + payloadSize);

    static constexpr size_t kMinBufferForDecode = 8192;
    static constexpr size_t kMaxFrameSize = 2048;

    if (m_mp3Buffer.size() < kMinBufferForDecode) {
        return;
    }

    mp3dec_frame_info_t info;
    while (m_mp3Buffer.size() > kMaxFrameSize) {
        std::vector<int16_t> pcmBuf(MINIMP3_MAX_SAMPLES_PER_FRAME);
        int samples = mp3dec_decode_frame(&m_mp3Decoder, m_mp3Buffer.data(),
                                           static_cast<int>(m_mp3Buffer.size()),
                                           pcmBuf.data(), &info);

        if (info.frame_bytes == 0) {
            break;
        }

        if (samples > 0 && info.channels > 0) {
            if (info.channels == 1) {
                std::vector<int16_t> stereoBuf(samples * 2);
                for (int i = 0; i < samples; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                size_t bytesToOutput = samples * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(stereoBuf.data()), bytesToOutput);
            } else {
                size_t bytesToOutput = static_cast<size_t>(samples) * info.channels * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmBuf.data()), bytesToOutput);
            }
        }

        m_mp3Buffer.erase(m_mp3Buffer.begin(), m_mp3Buffer.begin() + info.frame_bytes);
    }
}

void MiniaudioEngine::ClearBuffers(bool crossfade, int nextDurationSec) {
    std::lock_guard<std::mutex> lock(m_audioMutex);

    m_currentDurationSec = nextDurationSec;
    m_nearEndTriggered = false;
    m_finishedTriggered = false;
    m_playbackFrameCount = 0;

    if (crossfade && m_crossfadeDurationMs > 0) {
        InitiateCrossfade();
    } else {
        StopFadeOut();
        m_decoder.reset();
        m_pcmBuffer.Clear();
    }

    m_audioPid = 0x1FFF;
    m_id3BytesToSkip = 0;
    m_streamFormat = AudioStreamFormat::Unknown;
    m_mp3Buffer.clear();
    mp3dec_init(&m_mp3Decoder);

    std::lock_guard<std::mutex> netLock(m_networkMutex);
    m_aacBuffer.clear();
}

void MiniaudioEngine::InitiateCrossfade() {
    StopFadeOut();

    if (m_decoder) {
        m_fadeOutDecoder = std::move(m_decoder);
        m_fadeOutIsLocal = true;
    } else {
        m_fadeOutIsLocal = false;
        m_fadeOutPcm.clear();
        m_fadeOutPcmReadPos = 0;
        size_t avail = m_pcmBuffer.GetAvailableRead();
        if (avail > 0) {
            m_fadeOutPcm.resize(avail / sizeof(int16_t));
            m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(m_fadeOutPcm.data()), avail);
        }
    }
    m_pcmBuffer.Clear();

    m_crossfadeFramesTotal = (m_crossfadeDurationMs * 44100) / 1000;
    m_crossfadeFramesRemaining = m_crossfadeFramesTotal;
    m_isCrossfading = true;
}

void MiniaudioEngine::StopFadeOut() {
    m_fadeOutDecoder.reset();
    m_fadeOutPcm.clear();
    m_isCrossfading = false;
}