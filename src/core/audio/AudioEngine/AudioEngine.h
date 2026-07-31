#pragma once
#include <string>
#include "../../../miniaudio.h"

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool Initialize(const std::string& filePath);
    void Play();
    void Pause();
    void SetVolume(float volume); // От 0.0 до 1.0
    void Shutdown();

private:
    // Callback обязательно должен быть static для передачи в C-библиотеку
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    ma_decoder m_decoder;
    ma_device m_device;
    bool m_isInitialized;
    bool m_isPlaying;
};