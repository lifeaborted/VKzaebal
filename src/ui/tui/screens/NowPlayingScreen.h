#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include "models/Track.h"
#include "ui/tui/TuiThemeConfig.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_set>

class IAudioEngine;
class PlaylistManager;

namespace tui {

class CoverArtRenderer;
class SidebarComponent;

class NowPlayingScreen : public ftxui::ComponentBase {
public:
    NowPlayingScreen(IAudioEngine& audio,
                     PlaylistManager& playlist,
                     CoverArtRenderer& coverRenderer,
                     std::shared_ptr<SidebarComponent> sidebar);
    ~NowPlayingScreen() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateSpectrum(const std::vector<float>& fft);
    bool HasActiveSpectrum() const;
    void SetStatusMessage(const std::string& msg);
    void SetCurrentTrackLiked(bool liked) { m_isCurrentTrackLiked = liked; }
    void SetFavoriteTrackIds(const std::unordered_set<std::string>& ids) { m_favoriteTrackIds = ids; }
    void SetTrackLiked(const std::string& trackId, bool liked) {
        if (liked) m_favoriteTrackIds.insert(trackId);
        else m_favoriteTrackIds.erase(trackId);
    }
    void SetShowBottomBar(bool show) { m_showBottomBar = show; }
    void SetAutoScroll(bool enable) { m_autoScroll = enable; }
    bool GetAutoScroll() const { return m_autoScroll; }
    void SetIsOnline(bool online) { m_isOnline = online; }
    bool IsOnline() const { return m_isOnline; }

    void UpdateTheme(const ThemePalette& palette, const VisualizerConfig& visConfig);

    enum class VisualizerMode {
        SOLID_BARS = 0,       // 100% solid block equalizer
        SYMMETRIC_BARS = 1,   // Symmetrical solid wave
        SILK_WAVE = 2         // Smooth continuous contour wave
    };
    void CycleVisualizerMode();
    void AdvanceTicker() { m_tickerTick++; }

    // Callbacks to external controller
    std::function<void(double seconds)> OnSeekRequested;
    std::function<void(int trackIndex)> OnPlayTrackRequested;
    std::function<void()> OnTogglePlayPauseRequested;
    std::function<void()> OnNextTrackRequested;
    std::function<void()> OnPrevTrackRequested;
    std::function<void(float volume)> OnVolumeChanged;
    std::function<void()> OnToggleShuffleRequested;
    std::function<void()> OnCycleRepeatRequested;
    std::function<void(const Track& track)> OnToggleLike;
    std::function<void(const Track& track)> OnAddToPlaylist;
    std::function<void(const Track& track)> OnDownloadTrackRequested;

private:
    ftxui::Element RenderTopBlock();
    ftxui::Element RenderVisualizer(int availCols, int availRows);
    ftxui::Element RenderSolidBars(int availCols, int availRows);
    ftxui::Element RenderSymmetricBars(int availCols, int availRows);
    ftxui::Element RenderSilkWave(int availCols, int availRows);
    ftxui::Element RenderControls();
    ftxui::Element RenderQueue();

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    CoverArtRenderer& m_coverRenderer;
    std::shared_ptr<SidebarComponent> m_sidebar;

    std::string m_statusMessage;
    bool m_isCurrentTrackLiked = false;

    // Visualizer state
    VisualizerMode m_visMode = VisualizerMode::SOLID_BARS;
    static constexpr int kNumBars = 64;
    std::vector<float> m_spectrumPeaks;
    float m_sinePhase = 0.0f;
    ftxui::Box m_visBox;

    ThemePalette m_theme;
    VisualizerConfig m_visConfig;

    // Queue navigation & mouse hit testing
    int m_queueCursor = 0;
    int m_scrollOffset = 0;
    int m_visibleQueueRows = 10;
    int m_hoveredQueueRow = -1;
    int m_hoveredLikeRow = -1;
    int m_hoveredAddRow = -1;
    int m_hoveredDlRow = -1;
    int m_mouseX = -1;
    int m_mouseY = -1;
    std::vector<ftxui::Box> m_queueRowBoxes;
    std::vector<ftxui::Box> m_queueLikeBoxes;
    std::vector<ftxui::Box> m_queueAddBoxes;
    std::vector<ftxui::Box> m_queueDlBoxes;
    std::vector<int> m_visibleTrackIndices;
    std::unordered_set<std::string> m_favoriteTrackIds;

    // UI Interactive Boxes
    ftxui::Box m_seekBarBox;
    ftxui::Box m_playPauseBtnBox;
    ftxui::Box m_prevBtnBox;
    ftxui::Box m_nextBtnBox;
    ftxui::Box m_likeBtnBox;
    ftxui::Box m_shuffleBtnBox;
    ftxui::Box m_repeatBtnBox;
    ftxui::Box m_volMinusBox;
    ftxui::Box m_volPlusBox;
    ftxui::Box m_queueListBox;
    int m_tickerTick = 0;
    bool m_showBottomBar = true;
    bool m_autoScroll = true;
    bool m_isOnline = true;
    int m_lastActiveTrackIndex = -2;
    std::string m_lastActiveTrackId;
};

} // namespace tui
