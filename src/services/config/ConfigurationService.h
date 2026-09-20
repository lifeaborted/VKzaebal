#pragma once

#include <QString>
#include <string>

class ConfigurationService {
public:
    ConfigurationService();
    ~ConfigurationService() = default;

    void EnsureDefaultConfig();

    std::string GetActiveSource() const;
    void SetActiveSource(const std::string& source);

    bool GetShuffle() const;
    void SetShuffle(bool shuffle);

    int GetRepeatMode() const;
    void SetRepeatMode(int mode);

    float GetVolume() const;
    void SetVolume(float vol);

    bool GetAutoPlay() const;
    void SetAutoPlay(bool autoPlay);

    bool GetCrossfadeEnabled() const;
    void SetCrossfadeEnabled(bool enabled);

    int GetCrossfadeDurationMs() const;
    void SetCrossfadeDurationMs(int ms);

    int GetSavePositionMode() const;
    void SetSavePositionMode(int mode);

    bool GetShowVisualizer() const;
    void SetShowVisualizer(bool show);

    QString GetDownloadsPath() const;
    void SetDownloadsPath(const QString& path);
};
