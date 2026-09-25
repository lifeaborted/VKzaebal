#include "MiniaudioEngine.h"
#include "utils/logger/Logger.h"
#include "minimp3.h"
#include "utils/path/PathManager.h"

#include <QFile>
#include <QString>
#include <algorithm>
#include <chrono>
#include <QSettings>
#include <complex>
#include <cmath>
#include <numbers>

typedef std::complex<double> Complex;


MiniaudioEngine::MiniaudioEngine() : m_demuxer([this](const uint8_t* payload, size_t size, AudioFormat format) {
    if (format == AudioFormat::AAC_ADTS) {
        DecodeAacPayload(payload, size);
    } else {
        if (payload && size > 0) {
            m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + size);
        }
    }
}) {
    m_isDeviceInitialized = false;
    m_isDecoderInitialized = false;
    m_isPlaying = false;
    m_volume = 1.0f;
    m_mainBuffer.resize(16384, 0);
    m_fadeOutBuffer.resize(16384, 0);

    m_isDecoding = true;
    m_decodeThread = std::thread(&MiniaudioEngine::DecodeLoop, this);
}

MiniaudioEngine::~MiniaudioEngine() {
    if (m_isDeviceInitialized) {
        ma_device_stop(&m_device);
    }
    m_isDecoding = false;
    m_decodeCv.notify_all();

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
    if (m_isDeviceInitialized) {
        ma_device_uninit(&m_device);
        m_isDeviceInitialized = false;
    }
    StopFadeOut();
    m_decoder.reset();
    if (m_aacDecoder) {
        aacDecoder_Close(m_aacDecoder);
        m_aacDecoder = nullptr;
    }
    m_pcmBuffer.Clear();
}

float MiniaudioEngine::GetVolume() const { return m_volume; }

bool MiniaudioEngine::IsPlaying() const { return m_isPlaying; }

void MiniaudioEngine::SetPositionSeconds(double pos) {
    m_isNetworkFinished = false;
    if (m_currentDurationSec <= 0) return;
    if (pos < 0.0) pos = 0.0;
    if (pos > static_cast<double>(m_currentDurationSec)) pos = static_cast<double>(m_currentDurationSec);

    bool wasPlaying = m_isPlaying;
    if (wasPlaying && m_isDeviceInitialized) {
        m_isPlaying = false;
        ma_device_stop(&m_device);
    }

    bool isLocalFile = false;
    {
        // Блок 1: Изолированная работа с аудио-ядром
        std::lock_guard<std::mutex> lock(m_audioMutex);
        if (m_decoder) {
            isLocalFile = true;
            ma_uint64 targetFrame = static_cast<ma_uint64>(pos * static_cast<double>(SAMPLE_RATE));

            ma_uint64 seekFrame = (targetFrame > SAMPLE_RATE) ? (targetFrame - SAMPLE_RATE) : 0;
            ma_decoder_seek_to_pcm_frame(m_decoder.get(), seekFrame);

            ma_uint64 framesToRead = targetFrame - seekFrame;
            if (framesToRead > 0) {
                std::vector<int16_t> dumpBuf(framesToRead * 2);
                ma_uint64 framesReadTotal = 0;

                while (framesReadTotal < framesToRead) {
                    ma_uint64 toRead = framesToRead - framesReadTotal;
                    ma_uint64 read = 0;
                    ma_result result = ma_decoder_read_pcm_frames(m_decoder.get(),
                                                                  dumpBuf.data() + (framesReadTotal * 2),
                                                                  toRead, &read);
                    if (result != MA_SUCCESS || read == 0) {
                        break;
                    }
                    framesReadTotal += read;
                }
                m_playbackFrameCount = seekFrame + framesReadTotal;
            } else {
                m_playbackFrameCount = targetFrame;
            }

            m_nearEndTriggered = false;
            m_finishedTriggered = false;
            m_nearEndSignaled.store(false, std::memory_order_release);
            m_finishedSignaled.store(false, std::memory_order_release);
            StopFadeOut();
            double crossfadeSec = m_crossfadeDurationMs / 1000.0;
            m_seekedNearEnd.store(pos >= static_cast<double>(m_currentDurationSec) - crossfadeSec, std::memory_order_release);
            std::memset(m_mainBuffer.data(), 0, m_mainBuffer.size() * sizeof(int16_t));
            Logger::Log(LogLevel::INFO, "Miniaudio: Exact seeked to " + std::to_string(m_playbackFrameCount.load() / static_cast<double>(SAMPLE_RATE)) + "s");
        } else {
            m_playbackFrameCount = static_cast<ma_uint64>(pos * static_cast<double>(SAMPLE_RATE));
            m_nearEndTriggered = false;
            m_finishedTriggered = false;
            m_nearEndSignaled.store(false, std::memory_order_release);
            m_finishedSignaled.store(false, std::memory_order_release);
            StopFadeOut();
            double crossfadeSec = m_crossfadeDurationMs / 1000.0;
            m_seekedNearEnd.store(pos >= static_cast<double>(m_currentDurationSec) - crossfadeSec, std::memory_order_release);
            m_pcmBuffer.Clear();
        }
    }

    // Блок 2: Работа с сетью
    if (!isLocalFile) {
        m_isNetworkFinished = false;
        m_networkDiscardFrames = 0;
        {
            std::lock_guard<std::mutex> netLock(m_networkMutex);
            m_aacBuffer.clear();
            m_mp3Buffer.clear();
            m_mp3ReadOffset = 0;
            m_demuxer.Reset();
            if (m_aacDecoder) {
                aacDecoder_Close(m_aacDecoder);
                m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
            }
            mp3dec_init(&m_mp3Decoder);
        }
        m_decodeCv.notify_all();
        if (OnNetworkSeekRequested) {
            OnNetworkSeekRequested(pos);
        }
    }

    if (wasPlaying && m_isDeviceInitialized) {
        m_isPlaying = true;
        ma_device_start(&m_device);
    }
}

double MiniaudioEngine::GetPositionSeconds() const {
    return static_cast<double>(m_playbackFrameCount.load()) / static_cast<double>(SAMPLE_RATE);
}

double MiniaudioEngine::GetLengthSeconds() const {
    return static_cast<double>(m_currentDurationSec);
}

std::vector<float> MiniaudioEngine::GetSpectrumData() {
    std::vector<float> result(128, 0.0f);
    
    if (!m_isPlaying) return result;

    const size_t FFT_SIZE = 256;
    std::vector<Complex> complexData(FFT_SIZE);

    {
        // 1. Блокируем доступ и копируем последние 256 сэмплов из кэша
        std::unique_lock<std::mutex> specLock(m_spectrumMutex, std::try_to_lock);
        if (!specLock.owns_lock()) return result; // Если занято, отдаем нули

        // 2. Копирование с Окном Хеннинга
        for (size_t i = 0; i < FFT_SIZE; ++i) {
            double multiplier = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * i / (FFT_SIZE - 1)));
            complexData[i] = Complex(m_recentSamples[i] * multiplier, 0.0);
        }
    }

    // 3. Вызов алгоритма Кули-Тьюки
    m_fft.compute(complexData);

    // 4. Расчет итоговых амплитуд
    for (size_t i = 0; i < 128; ++i) {
        float mag = static_cast<float>(std::abs(complexData[i]) / 128.0) * 2.5f;
        result[i] = mag;
    }

    return result;
}

bool MiniaudioEngine::Init() {
    QSettings settings("config.ini", QSettings::IniFormat);
    m_crossfadeDurationMs = settings.value("Audio/CrossfadeDurationMs", 3000).toInt();

    m_pcmBuffer.Init(SAMPLE_RATE * 2 * sizeof(int16_t) * 5);
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_s16;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = SAMPLE_RATE;
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
        std::unique_lock<std::mutex> lock(engine->m_audioMutex, std::try_to_lock);

        if (!lock.owns_lock()) {
            std::memset(pOutput, 0, frameCount * 2 * sizeof(int16_t));
            return;
        }

        if (frameCount * 2 > engine->m_mainBuffer.size()) {
            frameCount = engine->m_mainBuffer.size() / 2;
        }

        std::memset(engine->m_mainBuffer.data(), 0, frameCount * 2 * sizeof(int16_t));
        std::memset(engine->m_fadeOutBuffer.data(), 0, frameCount * 2 * sizeof(int16_t));

        ma_uint32 framesRead = 0;

        if (engine->m_decoder) {
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(engine->m_decoder.get(), engine->m_mainBuffer.data(), frameCount, &read);
            framesRead = read;
        } else {
            ma_uint32 bytesToRead = frameCount * 2 * sizeof(int16_t);
            size_t bytesRead = engine->m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(engine->m_mainBuffer.data()), bytesToRead);
            framesRead = bytesRead / (2 * sizeof(int16_t));
        }

        engine->m_playbackFrameCount += framesRead;
        ma_uint32 fadeOutFramesRead = 0;

        if (engine->m_isCrossfading) {
            if (engine->m_fadeOutIsLocal && engine->m_fadeOutDecoder) {
                ma_uint64 read = 0;
                ma_decoder_read_pcm_frames(engine->m_fadeOutDecoder.get(), engine->m_fadeOutBuffer.data(), frameCount, &read);
                fadeOutFramesRead = read;
            } else {
                size_t elementsAvail = engine->m_fadeOutPcm.size() - engine->m_fadeOutPcmReadPos;
                size_t elementsToRead = frameCount * 2;
                if (elementsToRead > elementsAvail) elementsToRead = elementsAvail;

                if (elementsToRead > 0) {
                    std::memcpy(engine->m_fadeOutBuffer.data(), engine->m_fadeOutPcm.data() + engine->m_fadeOutPcmReadPos, elementsToRead * sizeof(int16_t));
                    engine->m_fadeOutPcmReadPos += elementsToRead;
                    fadeOutFramesRead = elementsToRead / 2;
                }
                if (fadeOutFramesRead == 0 && !engine->m_fadeOutIsLocal) {
                    engine->StopFadeOut();
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
                float s1 = (i < framesRead) ? engine->m_mainBuffer[idx] * mainVol : 0.0f;
                float s2 = (i < fadeOutFramesRead) ? engine->m_fadeOutBuffer[idx] * fadeOutVol : 0.0f;

                float mixed = s1 + s2;
                if (mixed > 32767.0f) mixed = 32767.0f;
                if (mixed < -32768.0f) mixed = -32768.0f;

                pOut[idx] = static_cast<int16_t>(mixed);
            }
        }

        {
            // Блокировка спектрограммы тоже стала NON-BLOCKING
            std::unique_lock<std::mutex> specLock(engine->m_spectrumMutex, std::try_to_lock);
            if (specLock.owns_lock()) {
                size_t samplesToCopy = std::min(static_cast<size_t>(frameCount), static_cast<size_t>(256));

                if (samplesToCopy > 0 && samplesToCopy < 256) {
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
        }

        double currentSec = static_cast<double>(engine->m_playbackFrameCount.load()) / static_cast<double>(SAMPLE_RATE);
        double totalSec = static_cast<double>(engine->m_currentDurationSec);
        double crossfadeSec = engine->m_crossfadeDurationMs / 1000.0;

        // --- ДЕТЕКТОР EOF ---
        if (framesRead == 0 && frameCount > 0) {
            static int s_underflowLogCount = 0;
            if (++s_underflowLogCount <= 5) {
                Logger::Log(LogLevel::WARNING, "audioCallback: underflow! currentSec=" + std::to_string(currentSec) +
                            " totalSec=" + std::to_string(totalSec) +
                            " isNetFin=" + std::to_string(engine->m_isNetworkFinished) +
                            " pcmAvailRead=" + std::to_string(engine->m_pcmBuffer.GetAvailableRead()) +
                            " aacBufSize=" + std::to_string(engine->m_aacBuffer.size()));
            }
            bool isEof = false;
            if (engine->m_decoder) {
                isEof = true;
            } else if (engine->m_isNetworkFinished && engine->m_pcmBuffer.GetAvailableRead() == 0) {
                if (totalSec <= 0.0 || currentSec >= totalSec - 5.0) {
                    isEof = true;
                }
            }

            if (isEof) {
                engine->m_playbackFrameCount = static_cast<ma_uint64>(totalSec * static_cast<double>(SAMPLE_RATE));
                currentSec = totalSec;
            }
        }

        if (totalSec > 0.0 || (framesRead == 0 && engine->m_decoder)) {
            if (totalSec > 0.0 && !engine->m_nearEndTriggered && currentSec >= totalSec - 10.0) {
                engine->m_nearEndTriggered = true;
                engine->m_nearEndSignaled.store(true, std::memory_order_release);
            }

            double endTriggerSec = totalSec;
            if (engine->m_isCrossfadeEnabled && engine->m_crossfadeDurationMs > 0 && totalSec > 0.0) {
                if (!engine->m_seekedNearEnd.load(std::memory_order_acquire)) {
                    endTriggerSec = totalSec - crossfadeSec;
                    if (endTriggerSec < 0.0) endTriggerSec = 0.0;
                }
            }

            if (!engine->m_finishedTriggered && currentSec >= endTriggerSec) {
                engine->m_finishedTriggered = true;
                engine->m_finishedSignaled.store(true, std::memory_order_release);
            }
        }
    }
}

bool MiniaudioEngine::PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) {
    m_currentTrackId = trackId;
    m_isCrossfadeEnabled = crossfade;

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
    m_nearEndSignaled.store(false, std::memory_order_release);
    m_finishedSignaled.store(false, std::memory_order_release);
    m_playbackFrameCount = 0;
    m_seekedNearEnd.store(false, std::memory_order_release);

    if (crossfade && m_crossfadeDurationMs > 0) {
        InitiateCrossfade();
    } else {
        StopFadeOut();
        m_decoder.reset();
        m_pcmBuffer.Clear();
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_s16, 2, SAMPLE_RATE);
    m_decoder.reset(new ma_decoder);

#ifdef _WIN32
    std::wstring wPath = localPath.toStdWString();
    if (ma_decoder_init_file_w(wPath.c_str(), &decoderConfig, m_decoder.get()) != MA_SUCCESS) {
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
    m_decodeCv.notify_one();
}

size_t MiniaudioEngine::GetNetworkBufferSize() const {
    std::lock_guard<std::mutex> lock(m_networkMutex);
    size_t mp3Remaining = (m_mp3Buffer.size() > m_mp3ReadOffset) ? (m_mp3Buffer.size() - m_mp3ReadOffset) : 0;
    return m_aacBuffer.size() + mp3Remaining;
}

void MiniaudioEngine::DecodeLoop() {
    while (m_isDecoding) {
        {
            std::unique_lock<std::mutex> lock(m_networkMutex);
            m_decodeCv.wait(lock, [this]() {
                bool hasAac = (m_aacBuffer.size() >= 188) || (m_isNetworkFinished && !m_aacBuffer.empty());
                bool hasMp3 = (m_mp3Buffer.size() - m_mp3ReadOffset >= 8192) ||
                              (m_isNetworkFinished && m_mp3Buffer.size() > m_mp3ReadOffset);
                return !m_isDecoding || hasAac || hasMp3;
            });

            if (!m_isDecoding) break;

            if (m_aacBuffer.empty() && (m_mp3Buffer.size() <= m_mp3ReadOffset)) {
                if (m_isNetworkFinished) {
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                continue;
            }
        }

        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        DecodeAACFrames();
    }
}

void MiniaudioEngine::DecodeAACFrames() {
    std::lock_guard<std::mutex> lock(m_networkMutex);

    static int s_aacLogCount = 0;
    if (++s_aacLogCount % 20 == 1) {
        Logger::Log(LogLevel::INFO, "DecodeAACFrames: aacBuf=" + std::to_string(m_aacBuffer.size()) +
                    " mp3Buf=" + std::to_string(m_mp3Buffer.size()) +
                    " mp3Offset=" + std::to_string(m_mp3ReadOffset) +
                    " pcmWrite=" + std::to_string(m_pcmBuffer.GetAvailableWrite()) +
                    " pcmRead=" + std::to_string(m_pcmBuffer.GetAvailableRead()));
    }

    if (m_demuxer.IsTsStreamDetermined() && !m_demuxer.IsTsStream()) {
        if (!m_aacBuffer.empty()) {
            m_demuxer.ProcessBytes(m_aacBuffer.data(), m_aacBuffer.size());
            m_aacBuffer.clear();
        }
        DecodeMp3Payload(nullptr, 0);
        return;
    }

    DecodeMp3Payload(nullptr, 0);

    size_t bytesConsumed = 0;
    while (m_aacBuffer.size() - bytesConsumed >= 188) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break;
        }

        m_demuxer.ProcessBytes(m_aacBuffer.data() + bytesConsumed, 188);
        bytesConsumed += 188;

        if (m_demuxer.IsTsStreamDetermined() && !m_demuxer.IsTsStream()) {
            break;
        }
    }

    if (!m_demuxer.IsTsStream() && bytesConsumed < m_aacBuffer.size()) {
        m_demuxer.ProcessBytes(m_aacBuffer.data() + bytesConsumed, m_aacBuffer.size() - bytesConsumed);
        bytesConsumed = m_aacBuffer.size();
    } else if (m_isNetworkFinished && bytesConsumed < m_aacBuffer.size() && m_pcmBuffer.GetAvailableWrite() >= 176400) {
        m_demuxer.ProcessBytes(m_aacBuffer.data() + bytesConsumed, m_aacBuffer.size() - bytesConsumed);
        bytesConsumed = m_aacBuffer.size();
    }

    if (bytesConsumed >= m_aacBuffer.size()) {
        m_aacBuffer.clear();
    } else if (bytesConsumed > 0) {
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + bytesConsumed);
    }

    if (m_pcmBuffer.GetAvailableWrite() >= 176400 || m_isNetworkFinished) {
        DecodeMp3Payload(nullptr, 0);
    }
}

void MiniaudioEngine::DecodeAacPayload(const uint8_t* payload, size_t payloadSize) {
    UCHAR* pBuffer = const_cast<UCHAR*>(payload);
    UINT bufferSize = static_cast<UINT>(payloadSize);
    UINT bytesValid = bufferSize;

    aacDecoder_Fill(m_aacDecoder, &pBuffer, &bufferSize, &bytesValid);

    int16_t pcmBuf[4096];
    int16_t stereoBuf[4096 * 2];

    while (true) {
        AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(m_aacDecoder, pcmBuf, 4096, 0);

        if (err == AAC_DEC_NOT_ENOUGH_BITS) break;
        if (err != AAC_DEC_OK) {
            static int s_errCount = 0;
            if (++s_errCount <= 10) {
                Logger::Log(LogLevel::ERROR, "aacDecoder_DecodeFrame error: 0x" + QString::number(err, 16).toStdString());
            }
            break;
        }

        CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
        if (info && info->numChannels > 0) {
            ma_uint32 framesToOutput = info->frameSize;
            int16_t* pcmDataPtr = nullptr;

            if (info->numChannels == 1) {
                int count = std::min<int>(info->frameSize, 4096);
                for (int i = 0; i < count; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                pcmDataPtr = stereoBuf;
            } else {
                pcmDataPtr = pcmBuf;
            }

            ma_uint64 discard = m_networkDiscardFrames.load();
            if (discard > 0) {
                ma_uint64 framesToDrop = std::min<ma_uint64>(discard, framesToOutput);
                m_networkDiscardFrames -= framesToDrop;
                framesToOutput -= framesToDrop;
                pcmDataPtr += (framesToDrop * 2);
            }

            if (framesToOutput > 0) {
                static int s_aacDecCount = 0;
                if (++s_aacDecCount <= 5) {
                    Logger::Log(LogLevel::INFO, "DecodeAacPayload: decoded " + std::to_string(framesToOutput) + " frames.");
                }
                size_t bytesToOutput = framesToOutput * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmDataPtr), bytesToOutput);
            }
        }
    }
}

void MiniaudioEngine::DecodeMp3Payload(const uint8_t* payload, size_t payloadSize) {
    if (payload && payloadSize > 0) {
        m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + payloadSize);
    }

    static constexpr size_t kMinBufferForDecode = 8192;
    static constexpr size_t kMaxFrameSize = 2048;

    bool isEof = m_isNetworkFinished && m_aacBuffer.empty();

    if (!isEof && (m_mp3Buffer.size() - m_mp3ReadOffset < kMinBufferForDecode)) {
        return;
    }

    int16_t pcmBuf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t stereoBuf[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    mp3dec_frame_info_t info;

    while (m_mp3Buffer.size() - m_mp3ReadOffset > 0) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break;
        }

        if (!isEof && (m_mp3Buffer.size() - m_mp3ReadOffset < kMaxFrameSize)) {
            break;
        }

        int samples = mp3dec_decode_frame(&m_mp3Decoder,
                                          m_mp3Buffer.data() + m_mp3ReadOffset,
                                          static_cast<int>(m_mp3Buffer.size() - m_mp3ReadOffset),
                                          pcmBuf, &info);

        if (samples == 0) {
            if (!isEof) {
                break;
            } else {
                if (info.frame_bytes > 0 && info.frame_bytes <= (m_mp3Buffer.size() - m_mp3ReadOffset)) {
                    m_mp3ReadOffset += info.frame_bytes;
                } else {
                    m_mp3ReadOffset++;
                }
                continue;
            }
        }

        if (info.channels > 0) {
            ma_uint32 framesToOutput = samples;
            int16_t* pcmDataPtr = nullptr;

            if (info.channels == 1) {
                int count = std::min<int>(samples, MINIMP3_MAX_SAMPLES_PER_FRAME);
                for (int i = 0; i < count; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                pcmDataPtr = stereoBuf;
            } else {
                pcmDataPtr = pcmBuf;
            }

            ma_uint64 discard = m_networkDiscardFrames.load();
            if (discard > 0) {
                ma_uint64 framesToDrop = std::min<ma_uint64>(discard, framesToOutput);
                m_networkDiscardFrames -= framesToDrop;
                framesToOutput -= framesToDrop;
                pcmDataPtr += (framesToDrop * 2);
            }

            if (framesToOutput > 0) {
                size_t bytesToOutput = framesToOutput * 2 * sizeof(int16_t);
                m_pcmBuffer.Write(reinterpret_cast<uint8_t*>(pcmDataPtr), bytesToOutput);
            }
        }

        m_mp3ReadOffset += info.frame_bytes;
    }

    if (m_mp3ReadOffset >= m_mp3Buffer.size()) {
        m_mp3Buffer.clear();
        m_mp3ReadOffset = 0;
    } else if (m_mp3ReadOffset >= 32768) {
        m_mp3Buffer.erase(m_mp3Buffer.begin(), m_mp3Buffer.begin() + m_mp3ReadOffset);
        m_mp3ReadOffset = 0;
    }
}

void MiniaudioEngine::ClearBuffers(bool crossfade, int nextDurationSec) {
    m_isNetworkFinished = false;

    {
        std::lock_guard<std::mutex> lock(m_audioMutex);

        m_currentDurationSec = nextDurationSec;
        m_isCrossfadeEnabled = crossfade;
        m_nearEndTriggered = false;
        m_finishedTriggered = false;
        m_nearEndSignaled.store(false, std::memory_order_release);
        m_finishedSignaled.store(false, std::memory_order_release);
        m_seekedNearEnd.store(false, std::memory_order_release);
        m_playbackFrameCount = 0;
        m_networkDiscardFrames = 0;

        if (crossfade && m_crossfadeDurationMs > 0) {
            InitiateCrossfade();
        } else {
            StopFadeOut();
            m_decoder.reset();
            m_pcmBuffer.Clear();
        }
    }

    {
        std::lock_guard<std::mutex> netLock(m_networkMutex);
        m_aacBuffer.clear();
        m_mp3Buffer.clear();
        m_mp3ReadOffset = 0;
        m_demuxer.Reset();
        if (m_aacDecoder) {
            aacDecoder_Close(m_aacDecoder);
            m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
        }
        mp3dec_init(&m_mp3Decoder);
    }
    m_decodeCv.notify_all();
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
        size_t frameBytes = 2 * sizeof(int16_t);
        size_t safeBytesToRead = (avail / frameBytes) * frameBytes;
        if (safeBytesToRead > 0) {
            m_fadeOutPcm.resize(safeBytesToRead / sizeof(int16_t));
            m_pcmBuffer.Read(reinterpret_cast<uint8_t*>(m_fadeOutPcm.data()), safeBytesToRead);
        }
    }
    m_pcmBuffer.Clear();

    if (m_fadeOutDecoder || !m_fadeOutPcm.empty()) {
        ma_uint32 targetFrames = static_cast<ma_uint32>((m_crossfadeDurationMs / 1000.0) * SAMPLE_RATE);
        if (!m_fadeOutIsLocal && !m_fadeOutPcm.empty()) {
            ma_uint32 availFrames = static_cast<ma_uint32>(m_fadeOutPcm.size() / 2);
            targetFrames = std::min(targetFrames, availFrames);
        }
        m_crossfadeFramesTotal = targetFrames;
        m_crossfadeFramesRemaining = targetFrames;
        m_isCrossfading = (targetFrames > 0);
    } else {
        m_isCrossfading = false;
    }
}

void MiniaudioEngine::StopFadeOut() {
    m_fadeOutDecoder.reset();
    m_fadeOutPcm.clear();
    m_isCrossfading = false;
}

void MiniaudioEngine::PollEvents() {
    if (m_nearEndSignaled.exchange(false, std::memory_order_acq_rel)) {
        if (OnTrackNearEnd) {
            OnTrackNearEnd();
        }
    }

    if (m_finishedSignaled.exchange(false, std::memory_order_acq_rel)) {
        if (OnTrackFinished) {
            OnTrackFinished();
        }
    }
}