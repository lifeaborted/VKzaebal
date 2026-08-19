#pragma once
#include <string>

class IAudioEngine;
class PlaylistManager;

class ConsoleRenderer {
public:
    ConsoleRenderer(IAudioEngine& audio, PlaylistManager& playlist);

    void Render();
    
    void SetVisualizerEnabled(bool enabled);
    bool IsVisualizerEnabled() const { return m_showVisualizer; }

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    
    bool m_showVisualizer = true;
    std::string m_lastPrintedStr = "";
};