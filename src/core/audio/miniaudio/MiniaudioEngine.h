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
#include <array>
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
    void PollEvents() override;
    size_t GetNetworkBufferSize() const override;

    void SetNetworkSkipSeconds(double seconds) override {
        m_networkDiscardFrames = static_cast<ma_uint64>(seconds * SAMPLE_RATE);
    }

    void SetNetworkStreamFinished() override {
        m_isNetworkFinished = true;
        m_decodeCv.notify_all();
    }

    void SetEqualizerEnabled(bool enabled) override;
    bool IsEqualizerEnabled() const override;
    void SetEqualizerBandGain(int bandIndex, float gainDb) override;
    float GetEqualizerBandGain(int bandIndex) const override;
    void SetEqualizerBands(const std::vector<float>& gainsDb) override;
    std::vector<float> GetEqualizerBands() const override;
    void SetEqualizerPreset(const std::string& presetName) override;
    std::string GetEqualizerPreset() const override;

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
    std::vector<float> m_mixBuffer;                                 // Буфер для микширования и 32-bit float DSP эквалайзера
    bool m_isDeviceInitialized = false;
    float m_volume = 1.0f;
    std::atomic<bool> m_isPlaying = false;
    std::unique_ptr<ma_decoder, DecoderDeleter> m_decoder;          // Декодер для чтения файлов
    bool m_isDecoderInitialized = false;                            // Флаг состояния декодера
    std::mutex m_audioMutex;                                        // Защита от конфликта потоков
    HANDLE_AACDECODER m_aacDecoder = nullptr;                       // Указатель на FDK-AAC декодер
    RingBuffer m_pcmBuffer;                                         // Потокобезопасный буфер для PCM
    std::vector<uint8_t> m_aacBuffer;                               // Временный буфер для сырых скачанных данных
    mutable std::mutex m_networkMutex;                              // Защита буфера скачивания
    std::atomic<size_t> m_atomicNetworkBufferSize{0};               // Неблокирующий размер сетевого буфера
    std::atomic<ma_uint64> m_playbackFrameCount{0};

    // --- ПЕРЕМЕННЫЕ КРОССФЕЙДА И ТАЙМИНГОВ ---
    bool m_isCrossfadeEnabled = false;
    int m_crossfadeDurationMs = 3000;
    int m_currentDurationSec = 0;
    bool m_nearEndTriggered = false;
    bool m_finishedTriggered = false;
    std::atomic<bool> m_nearEndSignaled{false};
    std::atomic<bool> m_finishedSignaled{false};
    std::atomic<bool> m_seekedNearEnd{false};

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

    // --- РЕСЕМПЛЕР СЕТЕВОГО ПОТОКА ---
    void EnsureResampler(ma_uint32 inSampleRate);
    void CleanupResampler();
    ma_resampler m_resampler;
    bool m_isResamplerInitialized = false;
    ma_uint32 m_currentInputSampleRate = 0;
    std::mutex m_resamplerMutex;

    mp3dec_t m_mp3Decoder;
    std::vector<uint8_t> m_mp3Buffer;
    size_t m_mp3ReadOffset = 0;
    MpegTsDemuxer m_demuxer;

    // --- ПЕРЕМЕННЫЕ ВИЗУАЛИЗАТОРА ---
    static constexpr size_t FFT_SIZE = 256;
    mutable std::mutex m_spectrumMutex;
    std::vector<float> m_recentSamples = std::vector<float>(FFT_SIZE, 0.0f);
    std::array<double, FFT_SIZE> m_hannWindow{};
    std::vector<Complex> m_fftComplexData;

    std::string m_currentTrackId;
    std::atomic<ma_uint64> m_networkDiscardFrames{0};

    std::atomic<bool> m_isNetworkFinished{false};

    FastFourierTransform m_fft{FFT_SIZE};
    
    // --- ПЕРЕМЕННЫЕ ЭКВАЛАЙЗЕРА ---
    static constexpr size_t EQ_NUM_BANDS = 10;
    static constexpr double EQ_FREQUENCIES[EQ_NUM_BANDS] = {
        31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };
    static constexpr double EQ_Q = 1.414;

    void InitEqualizer();
    void SaveEqualizerConfig();
    void LoadEqualizerConfig();

    std::atomic<bool> m_eqEnabled{false};
    std::string m_eqPreset = "Flat";
    std::array<float, EQ_NUM_BANDS> m_eqGains{};
    std::array<ma_peak2, EQ_NUM_BANDS> m_eqFilters{};
    bool m_eqFiltersInitialized = false;
};