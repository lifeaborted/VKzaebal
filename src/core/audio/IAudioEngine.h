#pragma once
#include <string>
#include <vector>
#include <functional>

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    // Основные методы плеера
    virtual bool Init() = 0;
    virtual bool PlayStream(const std::string& url, int durationSec, bool crossfade, const std::string& trackId) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void SetVolume(float volume) = 0;
    virtual void SetPositionSeconds(double pos) = 0;

    // Геттеры
    virtual float GetVolume() const = 0;
    virtual bool IsPlaying() const = 0;
    virtual double GetPositionSeconds() const = 0;
    virtual double GetLengthSeconds() const = 0;
    virtual std::vector<float> GetSpectrumData() const = 0;
    virtual void ClearBuffers(bool crossfade = false, int nextDurationSec = 0) = 0;
    virtual void SetNetworkSkipSeconds(double seconds) {}

    std::function<void()> OnTrackNearEnd;
    std::function<void()> OnTrackFinished;
    std::function<void(double)> OnNetworkSeekRequested;

    std::function<void(const std::string&)> OnPlaybackError;
};