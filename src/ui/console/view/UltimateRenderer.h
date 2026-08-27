#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>

struct RGB {
    int r, g, b;
};

class IAudioEngine;
class PlaylistManager;

class UltimateRenderer {
public:
    UltimateRenderer(IAudioEngine& audio, PlaylistManager& playlist);

    void Render();
    void ReloadConfig();

    int GetFramerate() const { return m_fps; }
    void RequestFullRedraw() { m_needsFullRedraw = true; }
    void SetStatusMessage(const std::string& msg);
    void SetOverlay(const std::string& msg);

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;

    mutable std::mutex m_renderMutex;

    int m_height = 10;
    int m_width = 40;
    int m_barWidth = 2;
    int m_barSpacing = 1;
    int m_blockSpacing = 0;

    int m_fps = 60;
    float m_smoothing = 0.5f;
    std::vector<float> m_previousPeaks;

    bool m_drawBorders = true;
    bool m_needsFullRedraw = false;

    std::string m_colorCode = "gradient";
    std::vector<RGB> m_gradientColors;

    bool m_bgEnabled = false;
    std::string m_bgColorCode = "gradient";
    std::vector<RGB> m_bgGradientColors;

    std::string m_layout = "Visualizer,ProgressBar,TrackInfo";
    std::string m_paddingLeft = "";

    std::string m_statusMessage = "";
    std::string m_overlayText = "";

    std::string m_lastPrintedStr = "";
    int m_lastLinesCount = 0;

    int m_uiFps = 5;
    std::chrono::steady_clock::time_point m_lastUiUpdate;

    // --- КЭШ ТЕКСТА ---
    std::string m_cTrackTitle;
    std::string m_cTimeStatus;
    std::string m_cProgressBar;
    std::string m_cVolumeEq;
    std::vector<std::string> m_cPlaylistLines;
    std::string m_cStatusMsg;
};