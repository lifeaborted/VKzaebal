#pragma once

#include "ui/tui/TuiTheme.h"
#include "services/database/DatabaseManager.h"
#include "models/Track.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tui {

struct PlaylistSelectionItem {
    int id = 0;
    std::string name;
    int trackCount = 0;
    bool alreadyContains = false;
};

class AddToPlaylistModalComponent : public ftxui::ComponentBase {
public:
    AddToPlaylistModalComponent();
    ~AddToPlaylistModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette);
    void Show(const Track& track, const std::vector<PlaylistSelectionItem>& items);
    void Hide();
    bool IsVisible() const { return m_isVisible; }

    // Callbacks
    std::function<void(int playlistId, const std::string& playlistName, const Track& track)> OnAddToPlaylistSelected;
    std::function<void()> OnCloseRequested;

private:
    std::string PluralizeTracks(int count) const;
    void SelectNextAvailable();
    void SelectPrevAvailable();
    void ConfirmSelection();

    bool m_isVisible = false;
    ThemePalette m_theme;
    Track m_track;
    std::vector<PlaylistSelectionItem> m_items;

    int m_selectedIndex = 0;
    int m_scrollOffset = 0;

    // Hover states for mouse
    int m_hoveredRow = -1;
    bool m_isCloseBtnHovered = false;

    // Interactive hit boxes
    ftxui::Box m_modalBox;
    ftxui::Box m_closeBtnBox;
    std::vector<ftxui::Box> m_rowBoxes;
    std::vector<ftxui::Box> m_addBtnBoxes;
};

} // namespace tui
