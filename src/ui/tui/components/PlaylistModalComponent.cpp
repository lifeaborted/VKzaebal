#include "PlaylistModalComponent.h"
#include "ui/tui/utils/TuiStringUtils.h"
#include <algorithm>
#include <unordered_set>

namespace tui {

PlaylistModalComponent::PlaylistModalComponent()
    : m_theme(GetDefaultTheme()) {
    ftxui::InputOption opt;
    opt.multiline = false;
    m_inputComponent = ftxui::Input(&m_newPlaylistName, "Название плейлиста...", opt);
    Add(m_inputComponent);
}

void PlaylistModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void PlaylistModalComponent::AutoSuggestName() {
    std::unordered_set<std::string> existing;
    for (const auto& pl : m_playlists) {
        existing.insert(pl.name);
    }
    int n = 1;
    while (true) {
        std::string candidate = "Плейлист " + std::to_string(n);
        if (existing.find(candidate) == existing.end()) {
            m_newPlaylistName = candidate;
            break;
        }
        n++;
    }
}

void PlaylistModalComponent::Show(const std::vector<PlaylistInfo>& playlists) {
    m_isVisible = true;
    m_playlists = playlists;
    m_isConfirmingDelete = false;
    m_playlistNameToDelete.clear();
    m_statusMessage.clear();
    AutoSuggestName();
    m_focusSection = FocusSection::INPUT;
    m_selectedPlaylistIndex = 0;
    m_scrollOffset = 0;
    if (m_inputComponent) {
        m_inputComponent->TakeFocus();
    }
}

void PlaylistModalComponent::Hide() {
    m_isVisible = false;
    m_isConfirmingDelete = false;
    m_playlistNameToDelete.clear();
    m_statusMessage.clear();
}

void PlaylistModalComponent::SetPlaylists(const std::vector<PlaylistInfo>& playlists) {
    m_playlists = playlists;
    if (m_selectedPlaylistIndex >= static_cast<int>(m_playlists.size())) {
        m_selectedPlaylistIndex = std::max(0, static_cast<int>(m_playlists.size()) - 1);
    }
    if (m_newPlaylistName.empty()) {
        AutoSuggestName();
    }
}

bool PlaylistModalComponent::IsTyping() const {
    return m_isVisible && (m_focusSection == FocusSection::INPUT);
}

void PlaylistModalComponent::HandleCreate() {
    // Trim string
    std::string trimmed = m_newPlaylistName;
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        m_statusMessage = "Название не может быть пустым";
        return;
    }
    size_t last = trimmed.find_last_not_of(" \t\r\n");
    trimmed = trimmed.substr(first, last - first + 1);

    for (const auto& pl : m_playlists) {
        if (pl.name == trimmed) {
            m_statusMessage = "Плейлист с таким именем уже существует";
            return;
        }
    }

    m_statusMessage.clear();
    if (OnCreatePlaylistRequested) {
        OnCreatePlaylistRequested(trimmed);
    }

    AutoSuggestName();
    m_focusSection = FocusSection::INPUT;
    if (m_inputComponent) {
        m_inputComponent->TakeFocus();
    }
}

ftxui::Element PlaylistModalComponent::Render() {
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
            ftxui::text(" ┌─ Управление плейлистами ") | ftxui::bold | ftxui::color(theme.accent),
            ftxui::filler(),
            closeBtn,
            ftxui::text(" ─┐ ") | ftxui::color(theme.accent)
        })
    );
    content.push_back(ftxui::text(""));

    // 2. Section 1: Create New Playlist
    content.push_back(
        ftxui::text("   [+] Создать новый плейлист:") | ftxui::bold | ftxui::color(theme.accentOrange)
    );
    content.push_back(ftxui::text(""));

    bool isInputFocused = (m_focusSection == FocusSection::INPUT);
    bool isCreateBtnFocused = (m_focusSection == FocusSection::CREATE_BTN);

    ftxui::Element inputElem = m_inputComponent->Render()
        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 34)
        | (isInputFocused ? ftxui::bold | ftxui::color(theme.text) | ftxui::bgcolor(theme.activeRow)
                          : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_inputBox);

    ftxui::Element createBtn = ftxui::text(" [ Создать ] ")
        | ftxui::bold
        | ((isCreateBtnFocused || m_isCreateBtnHovered)
               ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.accent)
               : ftxui::color(theme.accent))
        | ftxui::reflect(m_createBtnBox);

    content.push_back(
        ftxui::hbox({
            ftxui::text("   Название: ") | ftxui::color(theme.textMuted),
            inputElem,
            ftxui::text("  "),
            createBtn,
            ftxui::filler()
        })
    );

    if (!m_statusMessage.empty()) {
        content.push_back(ftxui::text(""));
        content.push_back(
            ftxui::text("   ! " + m_statusMessage) | ftxui::bold | ftxui::color(theme.accentRed)
        );
    }

    content.push_back(ftxui::text(""));
    content.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
    content.push_back(ftxui::text(""));

    // 3. Section 2: Existing Playlists or Delete Confirmation
    if (m_isConfirmingDelete) {
        content.push_back(
            ftxui::text("   Подтверждение удаления:") | ftxui::bold | ftxui::color(theme.accentRed)
        );
        content.push_back(ftxui::text(""));

        ftxui::Element yesBtn = ftxui::text(" [Да, удалить] ")
            | ftxui::bold
            | (m_isConfirmYesHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.accentRed)
                                     : ftxui::color(theme.accentRed))
            | ftxui::reflect(m_confirmYesBox);

        ftxui::Element noBtn = ftxui::text(" [Отмена] ")
            | (m_isConfirmNoHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                                    : ftxui::color(theme.textMuted))
            | ftxui::reflect(m_confirmNoBox);

        content.push_back(
            ftxui::hbox({
                ftxui::text("   Удалить плейлист '") | ftxui::color(theme.text),
                ftxui::text(m_playlistNameToDelete) | ftxui::bold | ftxui::color(theme.accentOrange),
                ftxui::text("'?  ") | ftxui::color(theme.text),
                yesBtn,
                ftxui::text("  "),
                noBtn,
                ftxui::filler()
            })
        );
        content.push_back(ftxui::text(""));
    } else {
        std::string headerStr = "   Существующие плейлисты (" + std::to_string(m_playlists.size()) + "):";
        content.push_back(ftxui::text(headerStr) | ftxui::bold | ftxui::color(theme.textMuted));
        content.push_back(ftxui::text(""));

        if (m_playlists.empty()) {
            content.push_back(
                ftxui::text("   (нет созданных плейлистов)") | ftxui::color(theme.textMuted)
            );
            content.push_back(ftxui::text(""));
        } else {
            const int kMaxDisplayRows = 7;
            int total = static_cast<int>(m_playlists.size());
            int endIndex = std::min(total, m_scrollOffset + kMaxDisplayRows);

            m_rowBoxes.clear();
            m_rowBoxes.resize(total);
            m_deleteBoxes.clear();
            m_deleteBoxes.resize(total);

            for (int i = m_scrollOffset; i < endIndex; ++i) {
                const auto& pl = m_playlists[i];
                bool isSelected = (i == m_selectedPlaylistIndex && m_focusSection == FocusSection::LIST);
                bool isHovered = (i == m_hoveredPlaylistRow);
                bool isDelHovered = (i == m_hoveredDeleteRow);

                std::string cursor = isSelected ? "► " : "  ";
                std::string num = std::to_string(i + 1) + ". ";
                std::string nameStr = pl.name;
                std::string countStr = "(" + std::to_string(pl.trackCount) + " " + utils::PluralizeTracks(pl.trackCount) + ")";

                ftxui::Element delBtn = ftxui::text(" [ Удалить ] ")
                    | ftxui::bold
                    | (isDelHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.accentRed)
                                    : ftxui::color(theme.accentRed))
                    | ftxui::reflect(m_deleteBoxes[i]);

                ftxui::Element rowElem = ftxui::hbox({
                    ftxui::text("  " + cursor + num)
                        | ftxui::color(isSelected ? theme.accent : theme.textMuted)
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 7),
                    ftxui::text(nameStr)
                        | (isSelected ? ftxui::bold | ftxui::color(theme.accent) : ftxui::color(theme.text))
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 34),
                    ftxui::text(countStr)
                        | ftxui::color(theme.textMuted)
                        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 16),
                    ftxui::filler(),
                    delBtn,
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
            content.push_back(ftxui::text(""));
        }
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

bool PlaylistModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) {
        return false;
    }

    // Mouse Handling
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_isCloseBtnHovered = m_closeBtnBox.Contain(mouse.x, mouse.y);
        m_isCreateBtnHovered = m_createBtnBox.Contain(mouse.x, mouse.y);
        m_isConfirmYesHovered = m_confirmYesBox.Contain(mouse.x, mouse.y);
        m_isConfirmNoHovered = m_confirmNoBox.Contain(mouse.x, mouse.y);

        m_hoveredPlaylistRow = -1;
        for (size_t i = 0; i < m_rowBoxes.size(); ++i) {
            if (m_rowBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredPlaylistRow = static_cast<int>(i);
                break;
            }
        }

        m_hoveredDeleteRow = -1;
        for (size_t i = 0; i < m_deleteBoxes.size(); ++i) {
            if (m_deleteBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredDeleteRow = static_cast<int>(i);
                break;
            }
        }

        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            // Click Close button or outside modal window -> close modal
            if (m_isCloseBtnHovered || !m_modalBox.Contain(mouse.x, mouse.y)) {
                Hide();
                if (OnCloseRequested) {
                    OnCloseRequested();
                }
                return true;
            }

            // Click Confirm Yes
            if (m_isConfirmingDelete && m_isConfirmYesHovered) {
                if (OnDeletePlaylistRequested) {
                    OnDeletePlaylistRequested(m_playlistNameToDelete);
                }
                m_isConfirmingDelete = false;
                return true;
            }

            // Click Confirm No
            if (m_isConfirmingDelete && m_isConfirmNoHovered) {
                m_isConfirmingDelete = false;
                return true;
            }

            // Click Create button
            if (m_isCreateBtnHovered) {
                HandleCreate();
                return true;
            }

            // Click Input box
            if (m_inputBox.Contain(mouse.x, mouse.y)) {
                m_focusSection = FocusSection::INPUT;
                if (m_inputComponent) {
                    m_inputComponent->TakeFocus();
                }
                return true;
            }

            // Click Delete button on a row
            if (m_hoveredDeleteRow >= 0 && m_hoveredDeleteRow < static_cast<int>(m_playlists.size())) {
                m_isConfirmingDelete = true;
                m_playlistNameToDelete = m_playlists[m_hoveredDeleteRow].name;
                return true;
            }

            // Click row -> select playlist
            if (m_hoveredPlaylistRow >= 0 && m_hoveredPlaylistRow < static_cast<int>(m_playlists.size())) {
                m_selectedPlaylistIndex = m_hoveredPlaylistRow;
                m_focusSection = FocusSection::LIST;
                return true;
            }
        }

        // Mouse wheel scroll for playlist list
        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (m_scrollOffset > 0) m_scrollOffset--;
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            if (m_scrollOffset + 7 < static_cast<int>(m_playlists.size())) m_scrollOffset++;
            return true;
        }

        return true;
    }

    // Keyboard Handling
    // Esc: Close confirmation or close modal
    if (event == ftxui::Event::Escape) {
        if (m_isConfirmingDelete) {
            m_isConfirmingDelete = false;
            return true;
        }
        Hide();
        if (OnCloseRequested) {
            OnCloseRequested();
        }
        return true;
    }

    // Delete Confirmation dialog hotkeys
    if (m_isConfirmingDelete) {
        if (event == ftxui::Event::Return ||
            event == ftxui::Event::Character('y') || event == ftxui::Event::Character('Y') ||
            event == ftxui::Event::Character("д") || event == ftxui::Event::Character("Д")) {
            if (OnDeletePlaylistRequested) {
                OnDeletePlaylistRequested(m_playlistNameToDelete);
            }
            m_isConfirmingDelete = false;
            return true;
        }
        if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N') ||
            event == ftxui::Event::Character("н") || event == ftxui::Event::Character("Н")) {
            m_isConfirmingDelete = false;
            return true;
        }
        return true;
    }

    // Tab: cycle between sections
    if (event == ftxui::Event::Tab) {
        if (m_focusSection == FocusSection::INPUT) {
            m_focusSection = FocusSection::CREATE_BTN;
        } else if (m_focusSection == FocusSection::CREATE_BTN) {
            if (!m_playlists.empty()) {
                m_focusSection = FocusSection::LIST;
            } else {
                m_focusSection = FocusSection::INPUT;
                if (m_inputComponent) m_inputComponent->TakeFocus();
            }
        } else if (m_focusSection == FocusSection::LIST) {
            m_focusSection = FocusSection::INPUT;
            if (m_inputComponent) m_inputComponent->TakeFocus();
        }
        return true;
    }

    // Enter / Return
    if (event == ftxui::Event::Return) {
        if (m_focusSection == FocusSection::INPUT || m_focusSection == FocusSection::CREATE_BTN) {
            HandleCreate();
            return true;
        }
        return true;
    }

    // Delete or D key when list item is focused
    if (event == ftxui::Event::Delete || event == ftxui::Event::Character('d') || event == ftxui::Event::Character('D')) {
        if (m_focusSection == FocusSection::LIST && !m_playlists.empty() &&
            m_selectedPlaylistIndex >= 0 && m_selectedPlaylistIndex < static_cast<int>(m_playlists.size())) {
            m_isConfirmingDelete = true;
            m_playlistNameToDelete = m_playlists[m_selectedPlaylistIndex].name;
            return true;
        }
    }

    // Arrow Up / Down
    if (event == ftxui::Event::ArrowUp) {
        if (m_focusSection == FocusSection::LIST) {
            if (m_selectedPlaylistIndex > 0) {
                m_selectedPlaylistIndex--;
                if (m_selectedPlaylistIndex < m_scrollOffset) {
                    m_scrollOffset = m_selectedPlaylistIndex;
                }
            } else {
                m_focusSection = FocusSection::INPUT;
                if (m_inputComponent) m_inputComponent->TakeFocus();
            }
            return true;
        }
    }

    if (event == ftxui::Event::ArrowDown) {
        if (m_focusSection == FocusSection::INPUT || m_focusSection == FocusSection::CREATE_BTN) {
            if (!m_playlists.empty()) {
                m_focusSection = FocusSection::LIST;
                m_selectedPlaylistIndex = 0;
            }
            return true;
        }
        if (m_focusSection == FocusSection::LIST) {
            if (m_selectedPlaylistIndex + 1 < static_cast<int>(m_playlists.size())) {
                m_selectedPlaylistIndex++;
                if (m_selectedPlaylistIndex >= m_scrollOffset + 7) {
                    m_scrollOffset = m_selectedPlaylistIndex - 6;
                }
            }
            return true;
        }
    }

    // If focused on Input, route remaining typing events to FTXUI Input
    if (m_focusSection == FocusSection::INPUT && m_inputComponent) {
        return m_inputComponent->OnEvent(event);
    }

    return true;
}

} // namespace tui
