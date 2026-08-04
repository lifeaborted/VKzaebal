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
    // Добавили флаг crossfade
    bool PlayStream(const std::string& url, int durationSec, bool crossfade);
    void Pause();
    void Resume();
    void SetVolume(float volume);
    void SetPositionSeconds(double pos);
    float GetVolume() const;
    bool IsPlaying() const;
    double GetPositionSeconds() const;
    double GetLengthSeconds() const;

    std::function<void()> OnTrackNearEnd;
    std::function<void()> OnTrackFinished;

private:
    HSTREAM m_activeStream = 0;   // Текущий играющий трек
    HSTREAM m_fadingStream = 0;   // Трек, который плавно затухает
    HSYNC m_syncCrossfade = 0;

    HPLUGIN m_hlsPlugin = 0;
    HSYNC m_syncEnd = 0;
    HSYNC m_syncNearEnd = 0;
    float m_volume = 1.0f;

    int m_crossfadeDurationMs = 3000;

    static void CALLBACK BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
    static void CALLBACK BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
};