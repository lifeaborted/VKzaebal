#pragma once
#include <string>
#include <functional>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "bass.h"
#ifdef _WIN32
#undef ERROR
#endif

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool Init();
    bool PlayStream(const std::string& url);
    void Pause();
    void Resume();
    void SetVolume(float volume);
    float GetVolume() const;
    bool IsPlaying() const;

    // Коллбэк, который будет вызываться, когда трек физически закончится
    std::function<void()> OnTrackFinished;

private:
    HSTREAM m_currentStream = 0;
    float m_volume = 1.0f;

    // Статический коллбэк для BASS, который прокинет вызов внутрь экземпляра класса
    static void CALLBACK BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
};