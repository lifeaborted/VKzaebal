#pragma once
#include "core/audio/IAudioEngine.h"
#include "miniaudio.h"
#include "utils/buffer/RingBuffer.h"
#include <string>
#include <atomic>
#include <vector>
#include <mutex>

class MiniaudioEngine : public IAudioEngine {
public:
    MiniaudioEngine();
    ~MiniaudioEngine() override;

    // --- Реализация интерфейса IAudioEngine ---
    bool Init() override;
    bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId = "") override;
    void Pause() override;
    void Resume() override;
    void SetVolume(float volume) override;
    void SetPositionSeconds(double pos) override;

    float GetVolume() const override;
    bool IsPlaying() const override;
    double GetPositionSeconds() const override;
    double GetLengthSeconds() const override;
    std::vector<float> GetSpectrumData() const override;

private:
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    ma_device m_device;
    bool m_isDeviceInitialized = false;

    float m_volume = 1.0f;
    std::atomic<bool> m_isPlaying = false;

    // todo:
    // - std::thread (поток для скачивания из сети)
    // - экземпляры RingBuffer для аудиоданных

    ma_decoder m_decoder;                // Декодер для чтения файлов
    bool m_isDecoderInitialized = false; // Флаг состояния декодера
    std::mutex m_audioMutex;             // Защита от конфликта потоков
};