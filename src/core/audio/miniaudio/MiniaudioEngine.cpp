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

MiniaudioEngine::MiniaudioEngine() : m_demuxer([this](const uint8_t* payload, size_t size, AudioFormat format) {
    if (format == AudioFormat::MP3) {
        DecodeMp3Payload(payload, size);
    } else {
        DecodeAacPayload(payload, size);
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
    m_isDecoding = false;
    m_decodeCv.notify_all();

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }
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
            std::memset(m_mainBuffer.data(), 0, m_mainBuffer.size() * sizeof(int16_t));
            Logger::Log(LogLevel::INFO, "Miniaudio: Exact seeked to " + std::to_string(m_playbackFrameCount.load() / static_cast<double>(SAMPLE_RATE)) + "s");
        } else {
            m_playbackFrameCount = static_cast<ma_uint64>(pos * static_cast<double>(SAMPLE_RATE));
            m_nearEndTriggered = false;
            m_finishedTriggered = false;
            m_pcmBuffer.Clear();
        }
    }

    // Блок 2: Работа с сетью
    if (!isLocalFile) {
        {
            std::lock_guard<std::mutex> netLock(m_networkMutex);
            m_aacBuffer.clear();
        }
        m_mp3Buffer.clear();
        m_demuxer.Reset();
        if (m_aacDecoder) {
            aacDecoder_Close(m_aacDecoder);
            m_aacDecoder = aacDecoder_Open(TT_MP4_ADTS, 1);
        }
        mp3dec_init(&m_mp3Decoder);
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

std::vector<float> MiniaudioEngine::GetSpectrumData() const {
    std::vector<float> result(128, 0.0f);
    if (!m_isPlaying) return result;

    std::vector<Complex> a(256);
    {
        std::unique_lock<std::mutex> lock(m_spectrumMutex, std::try_to_lock);
        if (!lock.owns_lock()) return result;

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
        }

        double currentSec = static_cast<double>(engine->m_playbackFrameCount.load()) / static_cast<double>(SAMPLE_RATE);
        double totalSec = static_cast<double>(engine->m_currentDurationSec);
        double crossfadeSec = engine->m_crossfadeDurationMs / 1000.0;

        if (framesRead == 0 && frameCount > 0) {
            bool isEof = false;
            if (engine->m_decoder) {
                isEof = true;
            } else if (totalSec > 0.0 && currentSec >= totalSec - 15.0) {
                isEof = true;
            }

            if (isEof) {
                currentSec = totalSec;
            }
        }

        if (totalSec > 0.0 || (framesRead == 0 && engine->m_decoder)) {
            if (totalSec > 0.0 && !engine->m_nearEndTriggered && currentSec >= totalSec - 10.0) {
                engine->m_nearEndTriggered = true;
                if (engine->OnTrackNearEnd) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [engine]() {
                        engine->OnTrackNearEnd();
                    }, Qt::QueuedConnection);
                }
            }

            double endTriggerSec = totalSec;
            if (engine->m_isCrossfadeEnabled && engine->m_crossfadeDurationMs > 0 && totalSec > 0.0) {
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
    m_playbackFrameCount = 0;

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

void MiniaudioEngine::DecodeLoop() {
    while (m_isDecoding) {
        {
            std::unique_lock<std::mutex> lock(m_networkMutex);
            m_decodeCv.wait(lock, [this]() {
                return !m_isDecoding || (m_aacBuffer.size() >= 188);
            });
        }

        if (!m_isDecoding) break;

        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        DecodeAACFrames();
    }
}

void MiniaudioEngine::DecodeAACFrames() {
    std::lock_guard<std::mutex> lock(m_networkMutex);

    size_t bytesConsumed = 0;
    while (m_aacBuffer.size() - bytesConsumed >= 188) {
        if (m_pcmBuffer.GetAvailableWrite() < 176400) {
            break;
        }

        m_demuxer.ProcessBytes(m_aacBuffer.data() + bytesConsumed, 188);
        bytesConsumed += 188;
    }

    if (bytesConsumed > 0) {
        m_aacBuffer.erase(m_aacBuffer.begin(), m_aacBuffer.begin() + bytesConsumed);
    }
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
        if (err != AAC_DEC_OK) break;

        CStreamInfo* info = aacDecoder_GetStreamInfo(m_aacDecoder);
        if (info && info->numChannels > 0) {
            ma_uint32 framesToOutput = info->frameSize;
            int16_t* pcmDataPtr = nullptr;
            std::vector<int16_t> stereoBuf;

            if (info->numChannels == 1) {
                stereoBuf.resize(info->frameSize * 2);
                for (int i = 0; i < info->frameSize; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                pcmDataPtr = stereoBuf.data();
            } else {
                pcmDataPtr = pcmBuf.data();
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
    }
}

void MiniaudioEngine::DecodeMp3Payload(const uint8_t* payload, size_t payloadSize) {
    m_mp3Buffer.insert(m_mp3Buffer.end(), payload, payload + payloadSize);

    static constexpr size_t kMinBufferForDecode = 8192;
    static constexpr size_t kMaxFrameSize = 2048;

    if (m_mp3Buffer.size() < kMinBufferForDecode) return;

    mp3dec_frame_info_t info;
    while (m_mp3Buffer.size() > kMaxFrameSize) {
        std::vector<int16_t> pcmBuf(MINIMP3_MAX_SAMPLES_PER_FRAME);
        int samples = mp3dec_decode_frame(&m_mp3Decoder, m_mp3Buffer.data(),
                                           static_cast<int>(m_mp3Buffer.size()),
                                           pcmBuf.data(), &info);

        if (info.frame_bytes == 0) break;

        if (samples > 0 && info.channels > 0) {
            ma_uint32 framesToOutput = samples;
            int16_t* pcmDataPtr = nullptr;
            std::vector<int16_t> stereoBuf;

            if (info.channels == 1) {
                stereoBuf.resize(samples * 2);
                for (int i = 0; i < samples; ++i) {
                    stereoBuf[i * 2]     = pcmBuf[i];
                    stereoBuf[i * 2 + 1] = pcmBuf[i];
                }
                pcmDataPtr = stereoBuf.data();
            } else {
                pcmDataPtr = pcmBuf.data();
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
        m_mp3Buffer.erase(m_mp3Buffer.begin(), m_mp3Buffer.begin() + info.frame_bytes);
    }
}

void MiniaudioEngine::ClearBuffers(bool crossfade, int nextDurationSec) {
    {
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
    }

    m_demuxer.Reset();
    m_mp3Buffer.clear();
    mp3dec_init(&m_mp3Decoder);

    {
        std::lock_guard<std::mutex> netLock(m_networkMutex);
        m_aacBuffer.clear();
    }
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

    m_crossfadeFramesTotal = (m_crossfadeDurationMs * SAMPLE_RATE) / 1000;
    m_crossfadeFramesRemaining = m_crossfadeFramesTotal;
    m_isCrossfading = true;
}

void MiniaudioEngine::StopFadeOut() {
    m_fadeOutDecoder.reset();
    m_fadeOutPcm.clear();
    m_isCrossfading = false;
}