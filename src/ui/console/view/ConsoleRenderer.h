#pragma once
#include <string>
#include <memory>

class IAudioEngine;
class PlaylistManager;
class UltimateRenderer;

class ConsoleRenderer {
public:
    ConsoleRenderer(IAudioEngine& audio, PlaylistManager& playlist);
    ~ConsoleRenderer();

    void Render();
    void ReloadConfig();

    int GetFramerate() const;
    void RequestFullRedraw();
    void SetStatusMessage(const std::string& msg);
    void SetOverlay(const std::string& msg);

    void SetVisualizerEnabled(bool enabled);
    bool IsVisualizerEnabled() const { return m_showVisualizer; }

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    std::unique_ptr<UltimateRenderer> m_ultimateRenderer;

    bool m_showVisualizer = true;
};