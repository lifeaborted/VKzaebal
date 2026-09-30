#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include "models/Track.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <functional>
#include <memory>

class IAudioEngine;
class PlaylistManager;

namespace tui {

class CoverArtRenderer;
class SidebarComponent;
enum class SearchMode {
    LOCAL,
    API
};

class SearchScreen : public ftxui::ComponentBase {
public:
    SearchScreen(IAudioEngine& audio,
                 PlaylistManager& playlist,
                 CoverArtRenderer& coverRenderer,
                 std::shared_ptr<SidebarComponent> sidebar);
    ~SearchScreen() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette) { m_theme = palette; }

    void SetSearchResults(const std::vector<Track>& results, const std::string& error = "");
    void AppendSearchResults(const std::vector<Track>& moreResults);
    void SetSearchLoading(bool loading);
    void SetSearchLoadingMore(bool loadingMore) { m_isLoadingMore = loadingMore; }
    bool HasMoreResults() const { return m_hasMoreResults; }
    bool IsLoadingMore() const { return m_isLoadingMore; }
    void FocusSearchInput();
    void DeactivateSearchInput() { m_isInputActive = false; }
    bool IsInputActive() const { return m_isInputActive; }
    std::string GetSearchQuery() const;
    void AdvanceTicker() { m_tickerTick++; }
    void SetShowBottomBar(bool show) { m_showBottomBar = show; }

    void SetSearchSource(const std::string& src);
    std::string GetSearchSource() const;

    void ToggleSearchMode();
    void SetSearchMode(SearchMode mode);
    SearchMode GetSearchMode() const { return m_searchMode; }

    void SetFavoriteTrackIds(const std::unordered_set<std::string>& favIds) { m_favoriteTrackIds = favIds; }
    void SetTrackLiked(const std::string& trackId, bool liked) {
        if (liked) m_favoriteTrackIds.insert(trackId);
        else m_favoriteTrackIds.erase(trackId);
    }
    bool IsTrackLiked(const std::string& trackId) const {
        return m_favoriteTrackIds.count(trackId) > 0;
    }

    // Callbacks to external controller
    std::function<void(const std::string& query, const std::string& source, SearchMode mode)> OnPerformSearch;
    std::function<void(const std::string& query, const std::string& source, int offset)> OnLoadMoreResults;
    std::function<void(SearchMode mode)> OnSearchModeChanged;
    std::function<void(const Track& track)> OnPlayTrackNow;
    std::function<void(int queueIndex)> OnPlayQueueIndexRequested;
    std::function<void(const Track& track)> OnEnqueueTrack;
    std::function<void(const Track& track)> OnLikeTrack;
    std::function<void(const Track& track)> OnDownloadTrack;
    std::function<void(double seconds)> OnSeekRequested;
    std::function<void()> OnTogglePlayPauseRequested;
    std::function<void()> OnNextTrackRequested;
    std::function<void()> OnPrevTrackRequested;
    std::function<void(float volume)> OnVolumeChanged;

private:
    ftxui::Element RenderCenterColumn();
    ftxui::Element RenderRightColumn();
    void CheckTriggerLoadMore();

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    CoverArtRenderer& m_coverRenderer;
    std::shared_ptr<SidebarComponent> m_sidebar;

    // Search state
    std::string m_searchQuery;
    std::string m_searchSource = "All";
    SearchMode m_searchMode = SearchMode::LOCAL;
    ftxui::Component m_inputComponent;
    bool m_isInputActive = false;
    ftxui::Box m_inputBox;
    ftxui::Box m_searchModeBox;
    int m_mouseX = -1;
    int m_mouseY = -1;
    bool m_isLoading = false;
    bool m_isLoadingMore = false;
    bool m_hasMoreResults = true;
    std::string m_errorMessage;
    std::vector<Track> m_searchResults;
    std::unordered_set<std::string> m_favoriteTrackIds;
    ThemePalette m_theme = GetDefaultTheme();

    // Results navigation & hit-testing
    bool m_isBrowsingResults = false;
    int m_selectedResultIndex = 0;
    int m_scrollOffset = 0;
    int m_tickerTick = 0;
    int m_hoveredDlRow = -1;
    std::vector<ftxui::Box> m_resultRowBoxes;
    std::vector<ftxui::Box> m_likeBtnBoxes;
    std::vector<ftxui::Box> m_addBtnBoxes;
    std::vector<ftxui::Box> m_dlBtnBoxes;
    std::vector<int> m_visibleResultIndices;
    ftxui::Box m_resultsListBox;

    // Mini player interactive boxes
    ftxui::Box m_miniPlayPauseBox;
    ftxui::Box m_miniPrevBox;
    ftxui::Box m_miniNextBox;
    ftxui::Box m_miniSeekBarBox;
    ftxui::Box m_miniVolMinusBox;
    ftxui::Box m_miniVolPlusBox;
    std::vector<ftxui::Box> m_upNextBoxes;
    bool m_showBottomBar = true;
};

} // namespace tui
