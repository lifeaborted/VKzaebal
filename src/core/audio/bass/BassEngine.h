#pragma once
#include "../IAudioEngine.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "bass.h"

#ifdef _WIN32
#undef ERROR
#endif

class BassEngine : public IAudioEngine {
public:
    BassEngine();
    ~BassEngine() override;

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
    std::vector<float> GetSpectrumData() const override;
    // Заглушка
    void ClearBuffers(bool crossfade = false, int nextDurationSec = 0) override {}

private:
    HSTREAM m_activeStream = 0;
    HSTREAM m_fadingStream = 0;
    HSYNC m_syncCrossfade = 0;

    HPLUGIN m_hlsPlugin = 0;
    HSYNC m_syncEnd = 0;
    HSYNC m_syncNearEnd = 0;
    float m_volume = 1.0f;

    int m_crossfadeDurationMs = 3000;

    static void CALLBACK BassTrackEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
    static void CALLBACK BassTrackNearEndCallback(HSYNC handle, DWORD channel, DWORD data, void* user);
};