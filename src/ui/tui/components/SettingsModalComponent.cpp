#include "SettingsModalComponent.h"
#include "services/config/ConfigurationService.h"
#include "ui/tui/TuiThemeConfig.h"
#include "ui/tui/modals/settings/GeneralSettingsTab.h"
#include "ui/tui/modals/settings/PlaybackSettingsTab.h"
#include "ui/tui/modals/settings/VisualizerSettingsTab.h"
#include "ui/tui/modals/settings/ThemeSettingsTab.h"
#include "ui/tui/modals/settings/ServiceColorsSettingsTab.h"
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace tui {

SettingsModalComponent::SettingsModalComponent(ConfigurationService& configService, TuiThemeConfig& themeConfig)
    : m_configService(configService),
      m_themeConfig(themeConfig),
      m_theme(GetDefaultTheme()) {
    m_tabs.push_back(std::make_unique<GeneralSettingsTab>(configService));
    m_tabs.push_back(std::make_unique<PlaybackSettingsTab>(configService));
    m_tabs.push_back(std::make_unique<VisualizerSettingsTab>(themeConfig));
    m_tabs.push_back(std::make_unique<ThemeSettingsTab>(themeConfig));
    m_tabs.push_back(std::make_unique<ServiceColorsSettingsTab>(themeConfig));

    for (auto& tab : m_tabs) {
        tab->LoadSettings();
    }
}

void SettingsModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void SettingsModalComponent::Show() {
    m_isVisible = true;
    m_isEditing = false;
    m_isDraggingScrollbar = false;
    m_selectedOption = 0;
    m_scrollOffset = 0;
    for (auto& tab : m_tabs) {
        tab->LoadSettings();
    }
}

void SettingsModalComponent::Hide() {
    m_isVisible = false;
    m_isEditing = false;
    m_isDraggingScrollbar = false;
}

void SettingsModalComponent::ChangeOptionValue(int delta) {
    if (m_selectedCategory >= 0 && m_selectedCategory < static_cast<int>(m_tabs.size())) {
        m_tabs[m_selectedCategory]->ChangeValue(m_selectedOption, delta);
        if (OnSettingsChanged) {
            OnSettingsChanged();
        }
    }
}

ftxui::Element SettingsModalComponent::RenderCategories() {
    auto theme = m_theme;
    m_categoryBoxes.clear();
    m_categoryBoxes.resize(m_tabs.size());

    std::vector<ftxui::Element> elems;
    elems.push_back(ftxui::text(" РАЗДЕЛЫ [Tab]") | ftxui::bold | ftxui::color(theme.textMuted));
    elems.push_back(ftxui::text(""));

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        bool isSelected = (static_cast<int>(i) == m_selectedCategory);
        bool isHovered = (static_cast<int>(i) == m_hoveredCategory);

        std::string prefix = isSelected ? "▶ " : "  ";
        ftxui::Element catElem = ftxui::text(prefix + m_tabs[i]->GetTitle());

        if (isSelected) {
            catElem = catElem | ftxui::bold | ftxui::color(theme.accent) | ftxui::bgcolor(theme.activeRow);
        } else if (isHovered) {
            catElem = catElem | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight);
        } else {
            catElem = catElem | ftxui::color(theme.text);
        }

        catElem = catElem | ftxui::reflect(m_categoryBoxes[i]);
        elems.push_back(std::move(catElem));
        elems.push_back(ftxui::text(""));
    }

    elems.push_back(ftxui::filler());

    return ftxui::vbox(std::move(elems))
        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 20);
}

ftxui::Element SettingsModalComponent::RenderOptionsList(int bodyHeight) {
    m_lastBodyHeight = bodyHeight;
    auto theme = m_theme;
    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(m_tabs.size())) {
        return ftxui::text("");
    }
    const auto& currentTab = m_tabs[m_selectedCategory];
    auto items = currentTab->GetItems();
    int totalOpts = static_cast<int>(items.size());

    int visibleItems = std::max(4, (bodyHeight + 1) / 3);
    int maxScroll = std::max(0, totalOpts - visibleItems);

    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);

    m_optionBoxes.resize(totalOpts);
    m_optMinusBoxes.resize(totalOpts);
    m_optPlusBoxes.resize(totalOpts);

    std::vector<ftxui::Element> rows;
    int endIdx = std::min(totalOpts, m_scrollOffset + visibleItems);

    for (int i = m_scrollOffset; i < endIdx; ++i) {
        const auto& item = items[i];
        bool isSel = (i == m_selectedOption);
        bool isEditingThis = (m_isEditing && m_editingCategory == m_selectedCategory && m_editingOption == i);
        bool isDisabled = currentTab->IsOptionDisabled(i);
        std::string rawVal = currentTab->GetCurrentValue(i);

        ftxui::Element rightControl;

        if (isDisabled) {
            rightControl = ftxui::text("< недоступно >") | ftxui::color(theme.textMuted);
        } else if (isEditingThis) {
            ftxui::Element swatchElem = ftxui::text("");
            if (item.type == SettingType::COLOR_INPUT) {
                ftxui::Color liveCol = TuiThemeConfig::ParseColor(m_editingBuffer, ftxui::Color::White);
                swatchElem = ftxui::hbox({
                    ftxui::text("[") | ftxui::color(theme.border),
                    ftxui::text("  ") | ftxui::bgcolor(liveCol),
                    ftxui::text("] ") | ftxui::color(theme.border)
                });
            } else if (item.type == SettingType::GRADIENT_INPUT) {
                auto stops = TuiThemeConfig::ParseColorList(m_editingBuffer, {});
                std::vector<ftxui::Element> stopBoxes;
                stopBoxes.push_back(ftxui::text("[") | ftxui::color(theme.border));
                for (const auto& s : stops) {
                    stopBoxes.push_back(ftxui::text(" ") | ftxui::bgcolor(ftxui::Color::RGB(s.r, s.g, s.b)));
                }
                stopBoxes.push_back(ftxui::text("] ") | ftxui::color(theme.border));
                swatchElem = ftxui::hbox(std::move(stopBoxes));
            }

            std::string before = m_editingBuffer.substr(0, std::clamp(m_editCursor, 0, static_cast<int>(m_editingBuffer.size())));
            ftxui::Element textWithCursor;
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                int next = m_editCursor + 1;
                while (next < static_cast<int>(m_editingBuffer.size()) && (static_cast<unsigned char>(m_editingBuffer[next]) & 0xC0) == 0x80) {
                    next++;
                }
                std::string curChar = m_editingBuffer.substr(m_editCursor, next - m_editCursor);
                std::string after = m_editingBuffer.substr(next);
                textWithCursor = ftxui::hbox({
                    ftxui::text(before) | ftxui::color(theme.text),
                    ftxui::text(curChar) | ftxui::color(ftxui::Color::Black) | ftxui::bgcolor(theme.accentOrange),
                    ftxui::text(after) | ftxui::color(theme.text)
                });
            } else {
                textWithCursor = ftxui::hbox({
                    ftxui::text(before) | ftxui::color(theme.text),
                    ftxui::text(" ") | ftxui::bgcolor(theme.accentOrange)
                });
            }

            rightControl = ftxui::hbox({
                swatchElem,
                ftxui::text("[") | ftxui::color(theme.border),
                textWithCursor,
                ftxui::text("] ") | ftxui::color(theme.border),
                ftxui::text("(Enter: сохранить, Esc: отмена)") | ftxui::color(theme.textMuted)
            });
        } else if (item.type == SettingType::CHOICE) {
            std::string displayVal = "< " + rawVal + " >";
            rightControl = ftxui::hbox({
                ftxui::text("[-] ") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optMinusBoxes[i]),
                ftxui::text(displayVal) | ftxui::bold | ftxui::color(theme.accentOrange),
                ftxui::text(" [>]") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optPlusBoxes[i])
            });
        } else if (item.type == SettingType::STEPPER) {
            std::string displayVal = "< " + rawVal + " >";
            rightControl = ftxui::hbox({
                ftxui::text("[-] ") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optMinusBoxes[i]),
                ftxui::text(displayVal) | ftxui::bold | ftxui::color(theme.accentOrange),
                ftxui::text(" [+]") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optPlusBoxes[i])
            });
        } else if (item.type == SettingType::TOGGLE) {
            bool isOn = (rawVal == "true" || rawVal == "1" || rawVal == "yes" || rawVal.find("[X]") != std::string::npos);
            std::string togStr = isOn ? "[X] Включено" : "[ ] Выключено";
            rightControl = ftxui::text(togStr) | ftxui::bold | ftxui::color(isOn ? theme.accent : theme.textMuted);
        } else if (item.type == SettingType::COLOR_INPUT) {
            ftxui::Color col = TuiThemeConfig::ParseColor(rawVal, ftxui::Color::White);
            rightControl = ftxui::hbox({
                ftxui::text("[") | ftxui::color(theme.border),
                ftxui::text("  ") | ftxui::bgcolor(col),
                ftxui::text("] ") | ftxui::color(theme.border),
                ftxui::text(rawVal) | ftxui::bold | ftxui::color(theme.accentOrange)
            });
        } else if (item.type == SettingType::GRADIENT_INPUT) {
            auto stops = TuiThemeConfig::ParseColorList(rawVal, {});
            std::vector<ftxui::Element> stopBoxes;
            stopBoxes.push_back(ftxui::text("[") | ftxui::color(theme.border));
            for (const auto& s : stops) {
                stopBoxes.push_back(ftxui::text(" ") | ftxui::bgcolor(ftxui::Color::RGB(s.r, s.g, s.b)));
            }
            stopBoxes.push_back(ftxui::text("] ") | ftxui::color(theme.border));

            rightControl = ftxui::hbox({
                ftxui::hbox(std::move(stopBoxes)),
                ftxui::text(" "),
                ftxui::text(rawVal) | ftxui::bold | ftxui::color(theme.accentOrange)
            });
        }

        std::string rowPrefix = isSel ? "▶ " : "  ";
        auto titleColor = isDisabled ? theme.textMuted : (isSel ? theme.accent : theme.text);

        ftxui::Element titleRow = ftxui::hbox({
            ftxui::text(rowPrefix + item.label)
                | (isSel ? ftxui::bold : ftxui::nothing)
                | ftxui::color(titleColor),
            ftxui::filler()
        });

        ftxui::Element valRow = ftxui::hbox({
            ftxui::text("      "),
            rightControl,
            ftxui::filler()
        });

        ftxui::Element itemBox = ftxui::vbox({
            titleRow,
            valRow
        });

        if (isSel) {
            itemBox = itemBox | ftxui::bgcolor(theme.activeRow);
        }
        itemBox = itemBox | ftxui::reflect(m_optionBoxes[i]);

        rows.push_back(std::move(itemBox));
        if (i + 1 < endIdx) {
            rows.push_back(ftxui::text(""));
        }
    }

    auto listContent = ftxui::vbox(std::move(rows)) | ftxui::flex;

    if (totalOpts > visibleItems) {
        std::vector<ftxui::Element> sbElems;
        bool canUp = (m_scrollOffset > 0);
        bool canDown = (m_scrollOffset < maxScroll);

        auto upBtn = ftxui::text(canUp ? "▲" : "─")
            | ftxui::color(canUp ? (m_isScrollUpHovered ? ftxui::Color::White : theme.accent) : theme.border)
            | (m_isScrollUpHovered && canUp ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollUpBox);
        sbElems.push_back(upBtn);

        int trackHeight = std::max(2, bodyHeight - 2);
        int thumbPos = (maxScroll > 0) ? (m_scrollOffset * (trackHeight - 1)) / maxScroll : 0;
        thumbPos = std::clamp(thumbPos, 0, trackHeight - 1);

        std::vector<ftxui::Element> trackRows;
        for (int t = 0; t < trackHeight; ++t) {
            if (t == thumbPos) {
                trackRows.push_back(ftxui::text("█") | ftxui::color(m_isScrollTrackHovered ? ftxui::Color::White : theme.accent));
            } else {
                trackRows.push_back(ftxui::text("│") | ftxui::color(theme.border));
            }
        }
        auto trackElem = ftxui::vbox(std::move(trackRows))
            | (m_isScrollTrackHovered ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollbarTrackBox);
        sbElems.push_back(trackElem);

        auto downBtn = ftxui::text(canDown ? "▼" : "─")
            | ftxui::color(canDown ? (m_isScrollDownHovered ? ftxui::Color::White : theme.accent) : theme.border)
            | (m_isScrollDownHovered && canDown ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollDownBox);
        sbElems.push_back(downBtn);

        auto scrollbar = ftxui::vbox(std::move(sbElems)) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);

        return ftxui::hbox({
            listContent,
            ftxui::text(" "),
            scrollbar
        }) | ftxui::flex;
    }

    return listContent;
}

ftxui::Element SettingsModalComponent::Render() {
    if (!m_isVisible) {
        return ftxui::text("");
    }

    auto theme = m_theme;
    size_t optCount = 0;
    if (m_selectedCategory >= 0 && m_selectedCategory < static_cast<int>(m_tabs.size())) {
        optCount = m_tabs[m_selectedCategory]->GetItems().size();
    }

    std::string countStr = " [" + std::to_string(m_selectedOption + 1) + "/" + std::to_string(optCount) + "] ";

    ftxui::Element closeBtn = ftxui::text(" [esc: закрыть] ")
        | (m_isCloseBtnHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_closeBtnBox);

    ftxui::Element topBar = ftxui::hbox({
        ftxui::text("  Настройки плеера ") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(countStr) | ftxui::color(theme.textMuted),
        ftxui::filler(),
        closeBtn,
        ftxui::text(" ")
    });

    int termWidth = ftxui::Terminal::Size().dimx;
    int termHeight = ftxui::Terminal::Size().dimy;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (winW > 20) termWidth = winW;
        if (winH > 10) termHeight = winH;
    }
#endif
    int modalWidth = std::clamp(static_cast<int>(termWidth * 0.78), 85, 115);
    int modalHeight = std::clamp(static_cast<int>(termHeight * 0.80), 26, 36);

    int overhead = m_showBottomBar ? 6 : 4;
    int bodyHeight = std::max(12, modalHeight - overhead);

    ftxui::Element body = ftxui::hbox({
        RenderCategories(),
        ftxui::separatorLight() | ftxui::color(theme.border),
        ftxui::text(" "),
        RenderOptionsList(bodyHeight) | ftxui::flex,
        ftxui::text(" ")
    }) | ftxui::flex;

    ftxui::Element footer;
    if (m_isEditing) {
        footer = ftxui::hbox({
            ftxui::filler(),
            ftxui::text(" [Ввод текста] RGB (R, G, B) или HEX (#RRGGBB)  [Enter] Сохранить  [Esc] Отмена ") | ftxui::bold | ftxui::color(theme.accentOrange),
            ftxui::filler()
        });
    } else {
        footer = ftxui::hbox({
            ftxui::filler(),
            ftxui::text(" [Tab] Разделы  [↑/↓] Пункт  [←/→] Значение  [Enter] Редактировать  [Esc] Закрыть ") | ftxui::color(theme.textMuted),
            ftxui::filler()
        });
    }

    std::vector<ftxui::Element> modalChildren;
    modalChildren.push_back(topBar);
    modalChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
    modalChildren.push_back(std::move(body) | ftxui::flex);
    if (m_showBottomBar) {
        modalChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
        modalChildren.push_back(footer);
    }

    auto modalWindow = ftxui::vbox(std::move(modalChildren))
        | ftxui::borderRounded
        | ftxui::color(theme.border)
        | ftxui::bgcolor(theme.panelBg)
        | ftxui::clear_under
        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, modalWidth)
        | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, modalHeight)
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

bool SettingsModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) {
        return false;
    }

    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(m_tabs.size())) {
        return false;
    }
    const auto& currentTab = m_tabs[m_selectedCategory];
    auto items = currentTab->GetItems();
    int maxOpts = static_cast<int>(items.size());
    int visibleItems = std::max(4, (m_lastBodyHeight + 1) / 3);
    int maxScroll = std::max(0, maxOpts - visibleItems);

    // 1. Text Editing Mode
    if (m_isEditing) {
        if (event == ftxui::Event::Return) {
            if (m_editingCategory >= 0 && m_editingCategory < static_cast<int>(m_tabs.size())) {
                m_tabs[m_editingCategory]->SetCurrentValue(m_editingOption, m_editingBuffer);
                if (OnSettingsChanged) OnSettingsChanged();
            }
            m_isEditing = false;
            return true;
        }

        if (event == ftxui::Event::Escape) {
            m_isEditing = false;
            return true;
        }

        if (event == ftxui::Event::Backspace) {
            if (!m_editingBuffer.empty() && m_editCursor > 0) {
                int prev = m_editCursor - 1;
                while (prev > 0 && (static_cast<unsigned char>(m_editingBuffer[prev]) & 0xC0) == 0x80) {
                    prev--;
                }
                m_editingBuffer.erase(prev, m_editCursor - prev);
                m_editCursor = prev;
            }
            return true;
        }

        if (event == ftxui::Event::ArrowLeft) {
            if (m_editCursor > 0) {
                m_editCursor--;
                while (m_editCursor > 0 && (static_cast<unsigned char>(m_editingBuffer[m_editCursor]) & 0xC0) == 0x80) {
                    m_editCursor--;
                }
            }
            return true;
        }

        if (event == ftxui::Event::ArrowRight) {
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                m_editCursor++;
                while (m_editCursor < static_cast<int>(m_editingBuffer.size()) &&
                       (static_cast<unsigned char>(m_editingBuffer[m_editCursor]) & 0xC0) == 0x80) {
                    m_editCursor++;
                }
            }
            return true;
        }

        if (event == ftxui::Event::Home) {
            m_editCursor = 0;
            return true;
        }

        if (event == ftxui::Event::End) {
            m_editCursor = static_cast<int>(m_editingBuffer.size());
            return true;
        }

        if (event == ftxui::Event::Delete) {
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                int next = m_editCursor + 1;
                while (next < static_cast<int>(m_editingBuffer.size()) &&
                       (static_cast<unsigned char>(m_editingBuffer[next]) & 0xC0) == 0x80) {
                    next++;
                }
                m_editingBuffer.erase(m_editCursor, next - m_editCursor);
            }
            return true;
        }

        if (event.is_character()) {
            std::string ch = event.character();
            m_editingBuffer.insert(m_editCursor, ch);
            m_editCursor += static_cast<int>(ch.size());
            return true;
        }

        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (m_editingCategory >= 0 && m_editingCategory < static_cast<int>(m_tabs.size())) {
                    m_tabs[m_editingCategory]->SetCurrentValue(m_editingOption, m_editingBuffer);
                    if (OnSettingsChanged) OnSettingsChanged();
                }
                m_isEditing = false;
            } else {
                return true;
            }
        } else {
            return true;
        }
    }

    // Toggle bottom hints panel with Shift+I or i / ш / Ш
    if (event == ftxui::Event::Character('I') || event == ftxui::Event::Character('i') ||
        event.character() == "Ш" || event.character() == "ш") {
        if (OnToggleBottomBarRequested) {
            OnToggleBottomBarRequested();
        } else {
            m_showBottomBar = !m_showBottomBar;
        }
        return true;
    }

    // 2. Mouse Handling
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_isCloseBtnHovered = m_closeBtnBox.Contain(mouse.x, mouse.y);
        m_isScrollUpHovered = m_scrollUpBox.Contain(mouse.x, mouse.y);
        m_isScrollDownHovered = m_scrollDownBox.Contain(mouse.x, mouse.y);
        m_isScrollTrackHovered = m_scrollbarTrackBox.Contain(mouse.x, mouse.y);

        m_hoveredCategory = -1;
        for (size_t i = 0; i < m_categoryBoxes.size(); ++i) {
            if (m_categoryBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredCategory = static_cast<int>(i);
                break;
            }
        }

        m_hoveredOption = -1;
        for (size_t i = 0; i < m_optionBoxes.size(); ++i) {
            if (m_optionBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredOption = static_cast<int>(i);
                break;
            }
        }

        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            if (m_scrollOffset < maxScroll) {
                m_scrollOffset++;
            }
            return true;
        }

        if (mouse.button == ftxui::Mouse::Left) {
            if (mouse.motion == ftxui::Mouse::Pressed) {
                // Close button or click outside modal
                if (m_isCloseBtnHovered || !m_modalBox.Contain(mouse.x, mouse.y)) {
                    Hide();
                    if (OnCloseRequested) OnCloseRequested();
                    return true;
                }

                // Interactive Scrollbar Up button
                if (m_isScrollUpHovered) {
                    if (m_scrollOffset > 0) {
                        m_scrollOffset--;
                    }
                    return true;
                }

                // Interactive Scrollbar Down button
                if (m_isScrollDownHovered) {
                    if (m_scrollOffset < maxScroll) {
                        m_scrollOffset++;
                    }
                    return true;
                }

                // Interactive Scrollbar Track Click / Drag
                if (m_isScrollTrackHovered || m_isDraggingScrollbar) {
                    m_isDraggingScrollbar = true;
                    int trackH = m_scrollbarTrackBox.y_max - m_scrollbarTrackBox.y_min;
                    if (trackH > 0 && maxScroll > 0) {
                        float ratio = static_cast<float>(mouse.y - m_scrollbarTrackBox.y_min) / static_cast<float>(trackH);
                        ratio = std::clamp(ratio, 0.0f, 1.0f);
                        m_scrollOffset = std::clamp(static_cast<int>(std::round(ratio * maxScroll)), 0, maxScroll);
                    }
                    return true;
                }

                // Category Click
                if (m_hoveredCategory >= 0) {
                    m_selectedCategory = m_hoveredCategory;
                    m_selectedOption = 0;
                    m_scrollOffset = 0;
                    return true;
                }

                // Option Minus button Click
                for (size_t i = 0; i < m_optMinusBoxes.size(); ++i) {
                    if (m_optMinusBoxes[i].Contain(mouse.x, mouse.y)) {
                        m_selectedOption = static_cast<int>(i);
                        ChangeOptionValue(-1);
                        return true;
                    }
                }

                // Option Plus button Click
                for (size_t i = 0; i < m_optPlusBoxes.size(); ++i) {
                    if (m_optPlusBoxes[i].Contain(mouse.x, mouse.y)) {
                        m_selectedOption = static_cast<int>(i);
                        ChangeOptionValue(+1);
                        return true;
                    }
                }

                // Option Row Click
                if (m_hoveredOption >= 0) {
                    m_selectedOption = m_hoveredOption;
                    const auto& item = items[m_selectedOption];
                    if (item.type == SettingType::COLOR_INPUT || item.type == SettingType::GRADIENT_INPUT) {
                        m_isEditing = true;
                        m_editingCategory = m_selectedCategory;
                        m_editingOption = m_selectedOption;
                        m_editingBuffer = currentTab->GetCurrentValue(m_selectedOption);
                        m_editCursor = static_cast<int>(m_editingBuffer.size());
                    } else {
                        ChangeOptionValue(+1);
                    }
                    return true;
                }
            } else if (mouse.motion == ftxui::Mouse::Released) {
                m_isDraggingScrollbar = false;
            }
        }
        return true;
    }

    // 3. Keyboard Handling
    if (event == ftxui::Event::Escape) {
        Hide();
        if (OnCloseRequested) OnCloseRequested();
        return true;
    }

    // Tab directly cycles categories
    int catCount = static_cast<int>(m_tabs.size());
    if (event == ftxui::Event::Tab) {
        m_selectedCategory = (m_selectedCategory + 1) % catCount;
        m_selectedOption = 0;
        m_scrollOffset = 0;
        return true;
    }
    if (event == ftxui::Event::TabReverse) {
        m_selectedCategory = (m_selectedCategory + catCount - 1) % catCount;
        m_selectedOption = 0;
        m_scrollOffset = 0;
        return true;
    }

    // Navigation inside options list
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k') || event == ftxui::Event::Character('K')) {
        if (m_selectedOption > 0) {
            m_selectedOption--;
            while (m_selectedOption > 0 && currentTab->IsOptionDisabled(m_selectedOption)) {
                m_selectedOption--;
            }
            if (m_selectedOption < m_scrollOffset) {
                m_scrollOffset = m_selectedOption;
            }
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j') || event == ftxui::Event::Character('J')) {
        if (m_selectedOption < maxOpts - 1) {
            m_selectedOption++;
            while (m_selectedOption < maxOpts - 1 && currentTab->IsOptionDisabled(m_selectedOption)) {
                m_selectedOption++;
            }
            if (currentTab->IsOptionDisabled(m_selectedOption)) {
                m_selectedOption--;
            }
            if (m_selectedOption >= m_scrollOffset + visibleItems) {
                m_scrollOffset = m_selectedOption - visibleItems + 1;
            }
        }
        return true;
    }

    if (event == ftxui::Event::ArrowLeft) {
        ChangeOptionValue(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowRight) {
        ChangeOptionValue(+1);
        return true;
    }

    if (event == ftxui::Event::Return || event == ftxui::Event::Character(' ')) {
        if (m_selectedOption >= 0 && m_selectedOption < maxOpts) {
            const auto& item = items[m_selectedOption];
            if (item.type == SettingType::COLOR_INPUT || item.type == SettingType::GRADIENT_INPUT) {
                m_isEditing = true;
                m_editingCategory = m_selectedCategory;
                m_editingOption = m_selectedOption;
                m_editingBuffer = currentTab->GetCurrentValue(m_selectedOption);
                m_editCursor = static_cast<int>(m_editingBuffer.size());
                return true;
            }
        }
        ChangeOptionValue(+1);
        return true;
    }

    return true;
}

} // namespace tui
