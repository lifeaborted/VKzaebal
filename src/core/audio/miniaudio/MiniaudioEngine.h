#pragma once
#include "core/audio/IAudioEngine.h"
#include "miniaudio.h"
#include "minimp3.h"
#include "utils/buffer/RingBuffer.h"
#include "aacdecoder_lib.h"
#include "utils/parser/MpegTsDemuxer.h"
#include <core/audio/fourierTransform/FourierTransform.h>

#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>


class MiniaudioEngine : public IAudioEngine {
public:
    MiniaudioEngine();
    ~MiniaudioEngine() override;

    bool Init() override;
    bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) override;
    void Pause() override;
    void Resume() override;
    void SetVolume(float volume) override;
    void SetPositionSeconds(double pos) override;

    float GetVolume() const override;
    bool IsPlaying() const override;
    double GetPositionSeconds() const override;
    double GetLengthSeconds() const override;
    std::vector<float> GetSpectrumData() override;

    // Метод, куда NetworkStreamer будет пушить скачанные байты AAC
    void PushNetworkData(const uint8_t* data, size_t size);
    void ClearBuffers(bool crossfade = false, int nextDurationSec = 0);

    void SetNetworkSkipSeconds(double seconds) override {
        m_networkDiscardFrames = static_cast<ma_uint64>(seconds * SAMPLE_RATE);
    }

    void SetNetworkStreamFinished() override {
        m_isNetworkFinished = true;
        m_decodeCv.notify_all();
    }

    static constexpr int SAMPLE_RATE = 44100;

private:
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void DecodeAACFrames();   // функция для декодирования ADTS пакетов
    void InitiateCrossfade(); // Вспомогательный метод кроссфейда
    void StopFadeOut();       // Вспомогательный метод очистки затухания
    void DecodeLoop();

    // Кастомный удалитель для декодера
    struct DecoderDeleter {
        void operator()(ma_decoder* dec) const {
            if (dec) {
                ma_decoder_uninit(dec);
                delete dec;
            }
        }
    };

    ma_device m_device;
    std::thread m_decodeThread;
    std::atomic<bool> m_isDecoding{false};
    std::condition_variable m_decodeCv;
    std::vector<int16_t> m_mainBuffer;
    std::vector<int16_t> m_fadeOutBuffer;
    bool m_isDeviceInitialized = false;
    float m_volume = 1.0f;
    std::atomic<bool> m_isPlaying = false;
    std::unique_ptr<ma_decoder, DecoderDeleter> m_decoder;          // Декодер для чтения файлов
    bool m_isDecoderInitialized = false;                            // Флаг состояния декодера
    std::mutex m_audioMutex;                                        // Защита от конфликта потоков
    HANDLE_AACDECODER m_aacDecoder = nullptr;                       // Указатель на FDK-AAC декодер
    RingBuffer m_pcmBuffer;                                         // Потокобезопасный буфер для PCM
    std::vector<uint8_t> m_aacBuffer;                               // Временный буфер для сырых скачанных данных
    std::mutex m_networkMutex;                                      // Защита буфера скачивания
    std::atomic<ma_uint64> m_playbackFrameCount{0};

    // --- ПЕРЕМЕННЫЕ КРОССФЕЙДА И ТАЙМИНГОВ ---
    bool m_isCrossfadeEnabled = false;
    int m_crossfadeDurationMs = 3000;
    int m_currentDurationSec = 0;
    bool m_nearEndTriggered = false;
    bool m_finishedTriggered = false;

    bool m_isCrossfading = false;
    ma_uint32 m_crossfadeFramesTotal = 0;
    ma_uint32 m_crossfadeFramesRemaining = 0;

    bool m_fadeOutIsLocal = false;
    std::unique_ptr<ma_decoder, DecoderDeleter> m_fadeOutDecoder;
    std::vector<int16_t> m_fadeOutPcm;
    size_t m_fadeOutPcmReadPos = 0;
    // -----------------------------------------

    void DecodeAacPayload(const uint8_t* payload, size_t payloadSize);
    void DecodeMp3Payload(const uint8_t* payload, size_t payloadSize);

    mp3dec_t m_mp3Decoder;
    std::vector<uint8_t> m_mp3Buffer;
    MpegTsDemuxer m_demuxer;

    // --- ПЕРЕМЕННЫЕ ВИЗУАЛИЗАТОРА ---
    mutable std::mutex m_spectrumMutex;
    std::vector<float> m_recentSamples = std::vector<float>(256, 0.0f);

    std::string m_currentTrackId;
    std::atomic<ma_uint64> m_networkDiscardFrames{0};

    std::atomic<bool> m_isNetworkFinished{false};

    FastFourierTransform m_fft{256};
};