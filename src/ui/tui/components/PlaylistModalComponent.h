#pragma once

#include "ui/tui/TuiTheme.h"
#include "services/database/DatabaseManager.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tui {

class PlaylistModalComponent : public ftxui::ComponentBase {
public:
    PlaylistModalComponent();
    ~PlaylistModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette);
    void Show(const std::vector<PlaylistInfo>& playlists);
    void Hide();
    bool IsVisible() const { return m_isVisible; }

    void SetPlaylists(const std::vector<PlaylistInfo>& playlists);

    // Callbacks
    std::function<void(const std::string& name)> OnCreatePlaylistRequested;
    std::function<void(const std::string& name)> OnDeletePlaylistRequested;
    std::function<void()> OnCloseRequested;

private:
    enum class FocusSection {
        INPUT,
        CREATE_BTN,
        LIST
    };

    void AutoSuggestName();
    void HandleCreate();
    std::string PluralizeTracks(int count) const;

    bool m_isVisible = false;
    ThemePalette m_theme;
    std::vector<PlaylistInfo> m_playlists;

    std::string m_newPlaylistName;
    std::string m_statusMessage;
    ftxui::Component m_inputComponent;

    FocusSection m_focusSection = FocusSection::INPUT;
    int m_selectedPlaylistIndex = 0;
    int m_scrollOffset = 0;

    // Hover states for mouse
    int m_hoveredPlaylistRow = -1;
    int m_hoveredDeleteRow = -1;
    bool m_isCreateBtnHovered = false;
    bool m_isCloseBtnHovered = false;

    // Delete confirmation state
    bool m_isConfirmingDelete = false;
    std::string m_playlistNameToDelete;
    bool m_isConfirmYesHovered = false;
    bool m_isConfirmNoHovered = false;

    // Interactive hit boxes
    ftxui::Box m_modalBox;
    ftxui::Box m_closeBtnBox;
    ftxui::Box m_inputBox;
    ftxui::Box m_createBtnBox;
    ftxui::Box m_confirmYesBox;
    ftxui::Box m_confirmNoBox;
    std::vector<ftxui::Box> m_rowBoxes;
    std::vector<ftxui::Box> m_deleteBoxes;
};

} // namespace tui
