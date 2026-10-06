#include "AddToPlaylistModalComponent.h"
#include "ui/tui/utils/TuiStringUtils.h"
#include <algorithm>

namespace tui {

AddToPlaylistModalComponent::AddToPlaylistModalComponent()
    : m_theme(GetDefaultTheme()) {
}

void AddToPlaylistModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void AddToPlaylistModalComponent::Show(const Track& track, const std::vector<PlaylistSelectionItem>& items) {
    m_isVisible = true;
    m_track = track;
    m_items = items;
    m_scrollOffset = 0;
    m_selectedIndex = -1;

    for (size_t i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].alreadyContains) {
            m_selectedIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_selectedIndex < 0 && !m_items.empty()) {
        m_selectedIndex = 0;
    }
}

void AddToPlaylistModalComponent::Hide() {
    m_isVisible = false;
    m_items.clear();
    m_track = Track();
    m_selectedIndex = 0;
    m_scrollOffset = 0;
}

void AddToPlaylistModalComponent::SelectNextAvailable() {
    if (m_items.empty()) return;
    int next = m_selectedIndex + 1;
    while (next < static_cast<int>(m_items.size())) {
        if (!m_items[next].alreadyContains) {
            m_selectedIndex = next;
            if (m_selectedIndex >= m_scrollOffset + 7) {
                m_scrollOffset = m_selectedIndex - 6;
            }
            return;
        }
        next++;
    }
}

void AddToPlaylistModalComponent::SelectPrevAvailable() {
    if (m_items.empty()) return;
    int prev = m_selectedIndex - 1;
    while (prev >= 0) {
        if (!m_items[prev].alreadyContains) {
            m_selectedIndex = prev;
            if (m_selectedIndex < m_scrollOffset) {
                m_scrollOffset = m_selectedIndex;
            }
            return;
        }
        prev--;
    }
}

void AddToPlaylistModalComponent::ConfirmSelection() {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        if (!m_items[m_selectedIndex].alreadyContains) {
            auto item = m_items[m_selectedIndex];
            Track trackCopy = m_track;
            Hide();
            if (OnAddToPlaylistSelected) {
                OnAddToPlaylistSelected(item.id, item.name, trackCopy);
            }
        }
    }
}

ftxui::Element AddToPlaylistModalComponent::Render() {
    if (!m_isVisible) {
        return ftxui::text("");
    }

    auto theme = m_theme;
    std::vector<ftxui::Element> content;

    // 1. Top bar: Title and Close button
    ftxui::Element closeBtn = ftxui::text(" [esc: закрыть] ")
        | (m_isCloseBtnHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_closeBtnBox);

    content.push_back(
        ftxui::hbox({
            ftxui::text(" ┌─ Добавление в плейлист ") | ftxui::bold | ftxui::color(theme.accent),
            ftxui::filler(),
            closeBtn,
            ftxui::text(" ─┐ ") | ftxui::color(theme.accent)
        })
    );
    content.push_back(ftxui::text(""));

    // 2. Track information
    std::string trackDesc = m_track.artist.empty() ? m_track.title : (m_track.artist + " — " + m_track.title);
    if (trackDesc.size() > 58) {
        trackDesc = trackDesc.substr(0, 55) + "...";
    }

    content.push_back(
        ftxui::hbox({
            ftxui::text("   Трек: ") | ftxui::color(theme.textMuted),
            ftxui::text(trackDesc) | ftxui::bold | ftxui::color(theme.text),
            ftxui::filler()
        })
    );

    content.push_back(ftxui::text(""));
    content.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
    content.push_back(ftxui::text(""));

    // 3. Playlists list
    content.push_back(
        ftxui::text("   Выберите плейлист:") | ftxui::bold | ftxui::color(theme.accentOrange)
    );
    content.push_back(ftxui::text(""));

    if (m_items.empty()) {
        content.push_back(
            ftxui::text("   (нет созданных плейлистов)") | ftxui::color(theme.textMuted)
        );
        content.push_back(
            ftxui::text("   Создайте плейлист через [P] или шестеренку в боковой панели") | ftxui::color(theme.textMuted)
        );
        content.push_back(ftxui::text(""));
    } else {
        const int kMaxDisplayRows = 7;
        int total = static_cast<int>(m_items.size());
        int endIndex = std::min(total, m_scrollOffset + kMaxDisplayRows);

        m_rowBoxes.clear();
        m_rowBoxes.resize(total);

        for (int i = m_scrollOffset; i < endIndex; ++i) {
            const auto& pl = m_items[i];
            bool isAlreadyIn = pl.alreadyContains;
            bool isSelected = (i == m_selectedIndex && !isAlreadyIn);
            bool isHovered = (i == m_hoveredRow && !isAlreadyIn);

            std::string num = std::to_string(i + 1) + ". ";
            std::string countStr = "(" + std::to_string(pl.trackCount) + " " + utils::PluralizeTracks(pl.trackCount) + ")";

            if (isAlreadyIn) {
                ftxui::Element rowElem = ftxui::hbox({
                    ftxui::text("    " + num)
                        | ftxui::color(theme.textMuted) | ftxui::dim
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 7),
                    ftxui::text(pl.name)
                        | ftxui::color(theme.textMuted) | ftxui::dim
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 34),
                    ftxui::text(countStr)
                        | ftxui::color(theme.textMuted) | ftxui::dim
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 16),
                    ftxui::filler(),
                    ftxui::text("(уже добавлен в плейлист) ")
                        | ftxui::color(theme.textMuted) | ftxui::dim,
                    ftxui::text(" ")
                }) | ftxui::reflect(m_rowBoxes[i]);

                content.push_back(std::move(rowElem));
            } else {
                std::string cursor = isSelected ? "► " : "  ";

                ftxui::Element addBtn = ftxui::text(" [ Добавить ] ")
                    | ftxui::bold
                    | (isHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.accent)
                                 : ftxui::color(theme.accent));

                ftxui::Element rowElem = ftxui::hbox({
                    ftxui::text("  " + cursor + num)
                        | ftxui::color(isSelected ? theme.accent : theme.textMuted)
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 7),
                    ftxui::text(pl.name)
                        | (isSelected ? ftxui::bold | ftxui::color(theme.accent) : ftxui::color(theme.text))
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 34),
                    ftxui::text(countStr)
                        | ftxui::color(theme.textMuted)
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 16),
                    ftxui::filler(),
                    addBtn,
                    ftxui::text("  ")
                });

                if (isSelected) {
                    rowElem = rowElem | ftxui::bgcolor(theme.activeRow);
                } else if (isHovered) {
                    rowElem = rowElem | ftxui::bgcolor(theme.highlight);
                }

                rowElem = rowElem | ftxui::reflect(m_rowBoxes[i]);
                content.push_back(std::move(rowElem));
            }
        }
        content.push_back(ftxui::text(""));
    }

    auto modalWindow = ftxui::vbox(std::move(content))
        | ftxui::borderRounded
        | ftxui::color(theme.border)
        | ftxui::bgcolor(theme.panelBg)
        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 78)
        | ftxui::clear_under
        | ftxui::reflect(m_modalBox);

    return ftxui::vbox({
        ftxui::filler(),
        ftxui::hbox({
            ftxui::filler(),
            modalWindow,
            ftxui::filler()
        }),
        ftxui::filler()
    });
}

bool AddToPlaylistModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) {
        return false;
    }

    // Mouse Handling
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_isCloseBtnHovered = m_closeBtnBox.Contain(mouse.x, mouse.y);

        m_hoveredRow = -1;
        for (size_t i = 0; i < m_rowBoxes.size(); ++i) {
            if (m_rowBoxes[i].Contain(mouse.x, mouse.y)) {
                if (i < m_items.size() && !m_items[i].alreadyContains) {
                    m_hoveredRow = static_cast<int>(i);
                }
                break;
            }
        }

        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            // Click Close button or outside modal window
            if (m_isCloseBtnHovered || !m_modalBox.Contain(mouse.x, mouse.y)) {
                Hide();
                if (OnCloseRequested) {
                    OnCloseRequested();
                }
                return true;
            }

            // Click an enabled row or its Add button
            if (m_hoveredRow >= 0 && m_hoveredRow < static_cast<int>(m_items.size())) {
                if (!m_items[m_hoveredRow].alreadyContains) {
                    m_selectedIndex = m_hoveredRow;
                    ConfirmSelection();
                    return true;
                }
            }
        }

        // Mouse wheel scroll
        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (m_scrollOffset > 0) m_scrollOffset--;
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            if (m_scrollOffset + 7 < static_cast<int>(m_items.size())) m_scrollOffset++;
            return true;
        }

        return true;
    }

    // Keyboard Handling
    if (event == ftxui::Event::Escape) {
        Hide();
        if (OnCloseRequested) {
            OnCloseRequested();
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown) {
        SelectNextAvailable();
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        SelectPrevAvailable();
        return true;
    }

    if (event == ftxui::Event::Return) {
        ConfirmSelection();
        return true;
    }

    return true;
}

} // namespace tui
