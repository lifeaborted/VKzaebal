#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tui {

struct SidebarItem {
    std::string id;
    std::string icon;
    std::string label;
    bool isPlaylist = false;
    bool isCreateButton = false;
    bool disabled = false;
};

class SidebarComponent : public ftxui::ComponentBase {
public:
    explicit SidebarComponent(std::function<void(const std::string& id, bool isPlaylist)> onSelect = nullptr);
    ~SidebarComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return false; }

    void SetOnSelectCallback(std::function<void(const std::string& id, bool isPlaylist)> onSelect);
    void SetOnCreatePlaylistCallback(std::function<void()> onCreatePlaylist);
    void SetOnManagePlaylistsCallback(std::function<void()> onManagePlaylists);
    void SetOnOpenSettingsCallback(std::function<void()> onOpenSettings) { m_onOpenSettings = std::move(onOpenSettings); }
    void SetOnOpenHelpCallback(std::function<void()> onOpenHelp) { m_onOpenHelp = std::move(onOpenHelp); }
    void SetSelectedId(const std::string& id);
    std::string GetSelectedId() const;
    bool IsSelectedPlaylist() const;

    void SetPlaylists(const std::vector<std::string>& playlistNames);
    void SetShowPlaylists(bool show);
    bool IsShowingPlaylists() const { return m_showPlaylists; }
    void SetItemDisabled(const std::string& id, bool disabled);
    bool IsItemDisabled(const std::string& id) const;
    void CycleNextSource();
    void CyclePrevSource();
    void SelectIndex(int index);
    void UpdateTheme(const ThemePalette& palette) { m_theme = palette; }
    void SetShowBottomBar(bool show) { m_showBottomBar = show; }

private:
    void RebuildItems();

    ThemePalette m_theme;
    std::vector<SidebarItem> m_items;
    std::vector<std::string> m_playlistNames;
    bool m_showPlaylists = true;
    int m_selectedIndex = 0;
    int m_hoveredIndex = -1;
    bool m_isGearHovered = false;
    bool m_isSettingsHovered = false;
    bool m_isHelpHovered = false;
    std::function<void(const std::string& id, bool isPlaylist)> m_onSelect;
    std::function<void()> m_onCreatePlaylist;
    std::function<void()> m_onManagePlaylists;
    std::function<void()> m_onOpenSettings;
    std::function<void()> m_onOpenHelp;

    std::vector<ftxui::Box> m_itemBoxes;
    ftxui::Box m_gearBox;
    ftxui::Box m_settingsBtnBox;
    ftxui::Box m_helpBtnBox;
    bool m_showBottomBar = true;
};

} // namespace tui
