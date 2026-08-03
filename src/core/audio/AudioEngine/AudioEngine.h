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
    bool PlayPreloaded();
    float GetVolume() const;
    bool IsPlaying() const;
    double GetPositionSeconds() const;
    double GetLengthSeconds() const;
    bool PreloadStream(const std::string& url);
    bool HasPreloadedStream() const { return m_nextStream != 0; }

    std::function<void()> OnTrackNearEnd;
    std::function<void()> OnTrackFinished;

private:
    HSTREAM m_currentStream = 0;
    HSTREAM m_nextStream = 0;
    HPLUGIN m_hlsPlugin = 0;
    HSYNC m_syncEnd = 0;
    HSYNC m_syncNearEnd = 0;
    float m_volume = 1.0f;

    static void CALLBACK BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
    static void CALLBACK BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
};