#include "SidebarComponent.h"
#include "ui/tui/TuiTheme.h"
#include <algorithm>

namespace tui {

SidebarComponent::SidebarComponent(std::function<void(const std::string& id, bool isPlaylist)> onSelect)
    : m_theme(GetDefaultTheme()),
      m_onSelect(std::move(onSelect)) {
    RebuildItems();
}

void SidebarComponent::SetOnSelectCallback(std::function<void(const std::string& id, bool isPlaylist)> onSelect) {
    m_onSelect = std::move(onSelect);
}

void SidebarComponent::SetOnCreatePlaylistCallback(std::function<void()> onCreatePlaylist) {
    m_onCreatePlaylist = std::move(onCreatePlaylist);
}

void SidebarComponent::SetOnManagePlaylistsCallback(std::function<void()> onManagePlaylists) {
    m_onManagePlaylists = std::move(onManagePlaylists);
}

void SidebarComponent::RebuildItems() {
    m_items.clear();

    // 1. Services
    m_items.push_back({"VK", "", "VKontakte", false, false});
    m_items.push_back({"Yandex", "", "Yandex", false, false});
    m_items.push_back({"SoundCloud", "", "SoundCloud", false, false});
    m_items.push_back({"YouTube", "", "YouTube", false, false});
    m_items.push_back({"Offline", "", "Offline", false, false});
    m_items.push_back({"All", "", "Все источники", false, false});

    // 2. Playlists: created playlists
    for (const auto& plName : m_playlistNames) {
        if (!plName.empty()) {
            m_items.push_back({plName, "", plName, true, false});
        }
    }

    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
        m_selectedIndex = 0;
    }

    m_itemBoxes.resize(m_items.size());
}

void SidebarComponent::SetPlaylists(const std::vector<std::string>& playlistNames) {
    m_playlistNames = playlistNames;
    std::string currentId = GetSelectedId();
    RebuildItems();
    SetSelectedId(currentId);
}

void SidebarComponent::SetShowPlaylists(bool show) {
    if (m_showPlaylists != show) {
        m_showPlaylists = show;
        if (!m_showPlaylists && IsSelectedPlaylist()) {
            SelectIndex(0);
        }
    }
}

void SidebarComponent::SetSelectedId(const std::string& id) {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_selectedIndex = static_cast<int>(i);
            return;
        }
    }
}

std::string SidebarComponent::GetSelectedId() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex].id;
    }
    return "VK";
}

bool SidebarComponent::IsSelectedPlaylist() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex].isPlaylist && !m_items[m_selectedIndex].isCreateButton;
    }
    return false;
}

void SidebarComponent::SetItemDisabled(const std::string& id, bool disabled) {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id) {
            m_items[i].disabled = disabled;
            if (disabled && m_selectedIndex == static_cast<int>(i)) {
                for (size_t j = 0; j < m_items.size(); ++j) {
                    if (!m_items[j].disabled && !m_items[j].isCreateButton) {
                        SelectIndex(static_cast<int>(j));
                        break;
                    }
                }
            }
            break;
        }
    }
}

void SidebarComponent::SelectIndex(int index) {
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        if (m_items[index].disabled) {
            return;
        }
        if (m_items[index].isCreateButton) {
            if (m_onCreatePlaylist) {
                m_onCreatePlaylist();
            }
            return;
        }
        if (m_selectedIndex == index) {
            return;
        }
        m_selectedIndex = index;
        if (m_onSelect) {
            m_onSelect(m_items[m_selectedIndex].id, m_items[m_selectedIndex].isPlaylist);
        }
    }
}

ftxui::Element SidebarComponent::Render() {
    if (!m_isVisible) return ftxui::emptyElement();

    auto theme = m_theme;
    std::vector<ftxui::Element> elements;

    elements.push_back(ftxui::text(""));

    // Header: Services
    elements.push_back(
        ftxui::hbox({
            ftxui::text(" СЕРВИСЫ") | ftxui::bold | ftxui::color(theme.textMuted)
        })
    );
    elements.push_back(ftxui::text(""));

    // 1. Render Services
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isPlaylist) continue;

        const auto& item = m_items[i];
        bool isSelected = (static_cast<int>(i) == m_selectedIndex);
        bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

        std::string prefix = isSelected ? "▶ " : "  ";
        std::string displayText = prefix + (item.icon.empty() ? "" : (item.icon + " ")) + item.label;

        ftxui::Element itemElem;
        if (item.disabled) {
            itemElem = ftxui::text(displayText) | ftxui::color(theme.textMuted) | ftxui::dim;
        } else {
            itemElem = ftxui::text(displayText);
            if (isSelected) {
                itemElem = itemElem | ftxui::bold | ftxui::color(theme.accent) | ftxui::bgcolor(theme.activeRow);
            } else if (isHovered) {
                itemElem = itemElem | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight);
            } else {
                itemElem = itemElem | ftxui::color(theme.text);
            }
        }

        if (i < m_itemBoxes.size()) {
            itemElem = itemElem | ftxui::reflect(m_itemBoxes[i]);
        }

        elements.push_back(std::move(itemElem));
    }

    // 2. Render Playlists section
    if (m_showPlaylists) {
        elements.push_back(ftxui::text(""));
        elements.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
        elements.push_back(ftxui::text(""));

        ftxui::Element gearBtn = ftxui::text(" \u26ED ")
            | ftxui::bold
            | (m_isGearHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.accentOrange))
            | ftxui::reflect(m_gearBox);

        elements.push_back(
            ftxui::hbox({
                ftxui::text(" ПЛЕЙЛИСТЫ") | ftxui::bold | ftxui::color(theme.textMuted),
                ftxui::filler(),
                gearBtn,
                ftxui::text(" ")
            })
        );
        elements.push_back(ftxui::text(""));

        size_t plCount = 0;
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (!m_items[i].isPlaylist) continue;
            plCount++;

            const auto& item = m_items[i];
            bool isSelected = (static_cast<int>(i) == m_selectedIndex);
            bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

            std::string prefix = isSelected ? "▶ " : "  ";
            std::string displayText = prefix + (item.icon.empty() ? "" : (item.icon + " ")) + item.label;

            ftxui::Element itemElem = ftxui::text(displayText);
            if (isSelected) {
                itemElem = itemElem | ftxui::bold | ftxui::color(theme.accent) | ftxui::bgcolor(theme.activeRow);
            } else if (isHovered) {
                itemElem = itemElem | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight);
            } else {
                itemElem = itemElem | ftxui::color(theme.text);
            }

            if (i < m_itemBoxes.size()) {
                itemElem = itemElem | ftxui::reflect(m_itemBoxes[i]);
            }

            elements.push_back(std::move(itemElem));
        }

        if (plCount == 0) {
            elements.push_back(
                ftxui::text("  (нет плейлистов)") | ftxui::color(theme.textMuted)
            );
        }
    }

    elements.push_back(ftxui::filler());
    elements.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
    elements.push_back(ftxui::text(""));

    const std::string kSettingsSymbol = "  \u26ED  ";
    const std::string kHelpSymbol     = "  ?  ";

    ftxui::Element settingsBtn = ftxui::text(kSettingsSymbol)
        | (m_isSettingsHovered ? ftxui::bold | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.accentOrange))
        | ftxui::reflect(m_settingsBtnBox);

    ftxui::Element helpBtn = ftxui::text(kHelpSymbol)
        | (m_isHelpHovered ? ftxui::bold | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                           : ftxui::color(theme.accentCyan))
        | ftxui::reflect(m_helpBtnBox);

    ftxui::Element bottomButtonsRow = ftxui::hbox({
        ftxui::filler(),
        settingsBtn,
        ftxui::text("   "),
        helpBtn,
        ftxui::filler()
    });

    elements.push_back(std::move(bottomButtonsRow));
    if (!m_showBottomBar) {
        elements.push_back(ftxui::text(""));
    }

    return ftxui::vbox(std::move(elements))
        | ftxui::bgcolor(theme.panelBg)
        | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 20)
        | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 24);
}

bool SidebarComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) return false;

    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_hoveredIndex = -1;
        m_isGearHovered = m_showPlaylists && m_gearBox.Contain(mouse.x, mouse.y);
        m_isSettingsHovered = m_settingsBtnBox.Contain(mouse.x, mouse.y);
        m_isHelpHovered = m_helpBtnBox.Contain(mouse.x, mouse.y);

        if (m_isGearHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            if (m_onManagePlaylists) {
                m_onManagePlaylists();
            }
            return true;
        }

        if (m_isSettingsHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            if (m_onOpenSettings) {
                m_onOpenSettings();
            }
            return true;
        }

        if (m_isHelpHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            if (m_onOpenHelp) {
                m_onOpenHelp();
            }
            return true;
        }

        for (size_t i = 0; i < m_itemBoxes.size(); ++i) {
            if (i < m_items.size() && m_items[i].isPlaylist && !m_showPlaylists) {
                continue;
            }
            if (i < m_items.size() && m_items[i].disabled) {
                continue;
            }
            if (m_itemBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredIndex = static_cast<int>(i);
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    SelectIndex(static_cast<int>(i));
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace tui
