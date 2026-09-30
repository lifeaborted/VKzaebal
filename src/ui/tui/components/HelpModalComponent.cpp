#include "HelpModalComponent.h"
#include "utils/path/PathManager.h"
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
#endif

namespace tui {

static std::string ScrollText(const std::string& text, int maxCols, int tick) {
    if (text.empty() || maxCols <= 0) return "";
    auto glyphs = ftxui::Utf8ToGlyphs(text);
    if (static_cast<int>(glyphs.size()) <= maxCols) {
        return text;
    }
    const int kGap = 4;
    int totalLen = static_cast<int>(glyphs.size()) + kGap;
    int startIdx = (tick > 0) ? (tick % totalLen) : 0;
    std::string result;
    result.reserve(maxCols * 4);
    for (int i = 0; i < maxCols; ++i) {
        int idx = (startIdx + i) % totalLen;
        if (idx < static_cast<int>(glyphs.size())) {
            result += glyphs[idx];
        } else {
            result += ' ';
        }
    }
    return result;
}

HelpModalComponent::HelpModalComponent()
    : m_theme(GetDefaultTheme()) {
}

void HelpModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void HelpModalComponent::Show(bool showSystemInfo) {
    m_isVisible = true;
    m_showSystemInfo = showSystemInfo;
    m_isSysInfoBtnFocused = false;
}

void HelpModalComponent::Hide() {
    m_isVisible = false;
    m_showSystemInfo = false;
    m_isSysInfoBtnFocused = false;
}

std::vector<HelpSection> HelpModalComponent::GetHelpSections() {
    return {
        {
            "ОСНОВНАЯ НАВИГАЦИЯ",
            {
                {"Space", "Пауза / Старт воспроизведения"},
                {"N / B", "Следующий / Предыдущий трек"},
                {"↑ / ↓", "Громкость (+5% / -5%)"},
                {"← / →", "Перемотка назад / вперед"},
                {"F / Esc", "Переход в Поиск / Главный экран"},
                {"Shift+I", "Скрыть / показать нижние подсказки"}
            }
        },
        {
            "ВОСПРОИЗВЕДЕНИЕ И ОЧЕРЕДЬ",
            {
                {"Tab", "Режим поиска (Локально / Сеть)"},
                {"Enter", "Включить выбранный трек"},
                {"+", "Добавить трек в конец очереди"},
                {"L", "Лайк / Избранное"},
                {"P", "Менеджер плейлистов"},
                {"V", "Стиль эквалайзера"}
            }
        },
        {
            "КОМАНДНАЯ СТРОКА (нажмите /)",
            {
                {"/v <0-100>", "Установить громкость (в %)"},
                {"/seek <сек>", "Перемотка на указанную секунду"},
                {"/next, /prev", "Следующий / предыдущий трек"},
                {"/pl <имя>", "Включить плейлист"},
                {"/settings", "Открыть окно настроек"},
                {"/logout <сервис>", "Сбросить авторизацию (VK, Ya...)"}
            }
        }
    };
}

std::vector<SystemPathInfo> HelpModalComponent::GetSystemPaths() {
    return {
        {"База данных (SQLite):", PathManager::GetDbPath().toStdString()},
        {"Файл настроек:", PathManager::GetConfigPath().toStdString()},
        {"Настройки TUI (ftxui.cfg):", PathManager::GetUltimateConfigPath().toStdString()},
        {"Папка загрузок музыки:", PathManager::GetDownloadsDir().toStdString()},
        {"Кэш обложек треков:", (PathManager::GetCacheDir() + "/covers/").toStdString()},
        {"Папка текстов песен:", PathManager::GetLyricsDir().toStdString()},
        {"Сетевой дисковый кэш:", (PathManager::GetCacheDir() + "/http_cache/").toStdString()},
        {"Журнал логов программы:", PathManager::GetLogFilePath().toStdString()},
        {"Временные потоковые файлы:", PathManager::GetTempDir().toStdString()}
    };
}

ftxui::Element HelpModalComponent::Render() {
    if (!m_isVisible) {
        return ftxui::text("");
    }
    m_animTick++;
    if (m_showSystemInfo) {
        return RenderSystemInfoView();
    }
    return RenderHelpView();
}

ftxui::Element HelpModalComponent::RenderHelpView() {
    auto theme = m_theme;

    // 1. Top bar: Title and Close button
    ftxui::Element closeBtn = ftxui::text(" [esc: закрыть] ")
        | (m_isCloseBtnHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_closeBtnBox);

    ftxui::Element topBar = ftxui::hbox({
        ftxui::text(" ┌─ Справка по горячим клавишам и командам ") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::filler(),
        closeBtn,
        ftxui::text(" ─┐ ") | ftxui::color(theme.accent)
    });

    // 2. Commands list in single-column layout (scrollable up and down)
    auto sections = GetHelpSections();
    std::vector<ftxui::Element> allRows;

    for (size_t sIdx = 0; sIdx < sections.size(); ++sIdx) {
        const auto& sec = sections[sIdx];
        allRows.push_back(
            ftxui::text("  " + sec.title) | ftxui::bold | ftxui::color(theme.accentOrange)
        );

        for (const auto& item : sec.items) {
            int keyCols = static_cast<int>(ftxui::Utf8ToGlyphs(item.key).size());
            int pad = std::max(2, 22 - keyCols);
            std::string padding(pad, ' ');
            allRows.push_back(
                ftxui::hbox({
                    ftxui::text("    " + item.key + padding) | ftxui::bold | ftxui::color(theme.accentCyan),
                    ftxui::text(item.desc) | ftxui::color(theme.text)
                })
            );
        }
        if (sIdx + 1 < sections.size()) {
            allRows.push_back(ftxui::text(""));
        }
    }

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
    int modalWidth = std::clamp(static_cast<int>(termWidth * 0.70), 75, 105);
    int modalHeight = std::clamp(static_cast<int>(termHeight * 0.70), 20, 32);

    // Calculate vertical slice for scrolling
    int visibleContentHeight = std::max(5, modalHeight - 6);
    int maxScroll = std::max(0, static_cast<int>(allRows.size()) - visibleContentHeight);
    m_helpScrollOffset = std::clamp(m_helpScrollOffset, 0, maxScroll);

    std::vector<ftxui::Element> visibleRows;
    for (int i = m_helpScrollOffset; i < std::min(static_cast<int>(allRows.size()), m_helpScrollOffset + visibleContentHeight); ++i) {
        visibleRows.push_back(std::move(allRows[i]));
    }

    const std::string kSysInfoBtnText = " 🛈 Дополнительная информация ";

    bool isBtnActive = m_isSysInfoBtnHovered || m_isSysInfoBtnFocused;
    ftxui::Element sysInfoBtn = ftxui::text(kSysInfoBtnText)
        | ftxui::bold
        | (isBtnActive ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                       : ftxui::color(theme.accent) | ftxui::bgcolor(theme.activeRow))
        | ftxui::reflect(m_sysInfoBtnBox);

    ftxui::Element sysInfoRow = ftxui::hbox({
        ftxui::filler(),
        sysInfoBtn,
        ftxui::filler()
    });

    std::vector<ftxui::Element> modalChildren;
    modalChildren.push_back(std::move(topBar));
    modalChildren.push_back(ftxui::text(""));
    modalChildren.push_back(ftxui::vbox(std::move(visibleRows)) | ftxui::flex);
    if (m_showBottomBar) {
        modalChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
        modalChildren.push_back(std::move(sysInfoRow));
    }

    ftxui::Element modalWindow = ftxui::vbox(std::move(modalChildren))
    | ftxui::border
    | ftxui::bgcolor(theme.panelBg)
    | ftxui::color(theme.border)
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

ftxui::Element HelpModalComponent::RenderSystemInfoView() {
    auto theme = m_theme;

    // Header
    ftxui::Element backBtn = ftxui::text(" [esc: назад] ")
        | (m_isSysInfoBackBtnHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                                     : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_sysInfoBackBtnBox);

    ftxui::Element topBar = ftxui::hbox({
        ftxui::text(" ┌─ Системные пути и директории ") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::filler(),
        backBtn,
        ftxui::text(" ─┐ ") | ftxui::color(theme.accent)
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
    int modalWidth = std::clamp(static_cast<int>(termWidth * 0.70), 75, 110);
    int modalHeight = std::clamp(static_cast<int>(termHeight * 0.75), 22, 32);

    // Paths section: label on line 1, path value on line 2 with larger indent + dynamic marquee
    std::vector<ftxui::Element> allPathRows;
    allPathRows.push_back(
        ftxui::text("  ПУТИ К ДАННЫМ И КОНФИГУРАЦИИ") | ftxui::bold | ftxui::color(theme.accentOrange)
    );
    allPathRows.push_back(ftxui::text(""));

    auto paths = GetSystemPaths();
    int availPathWidth = std::max(10, modalWidth - 10);

    for (size_t pIdx = 0; pIdx < paths.size(); ++pIdx) {
        const auto& pi = paths[pIdx];

        // Line 1: Label with same indent as section title (2 spaces)
        allPathRows.push_back(
            ftxui::text("  " + pi.label) | ftxui::bold | ftxui::color(theme.accentCyan)
        );

        // Line 2: Path value with larger indent (6 spaces) + dynamic marquee scrolling
        std::string scrolledPath = ScrollText(pi.path, availPathWidth, m_animTick);
        allPathRows.push_back(
            ftxui::text("      " + scrolledPath) | ftxui::color(theme.text)
        );

        if (pIdx + 1 < paths.size()) {
            allPathRows.push_back(ftxui::text(""));
        }
    }

    int visibleHeight = std::max(4, modalHeight - 6);
    int maxVScroll = std::max(0, static_cast<int>(allPathRows.size()) - visibleHeight);
    m_sysInfoVScroll = std::clamp(m_sysInfoVScroll, 0, maxVScroll);

    std::vector<ftxui::Element> visiblePathRows;
    for (int i = m_sysInfoVScroll; i < std::min(static_cast<int>(allPathRows.size()), m_sysInfoVScroll + visibleHeight); ++i) {
        visiblePathRows.push_back(std::move(allPathRows[i]));
    }

    ftxui::Element modalWindow = ftxui::vbox({
        std::move(topBar),
        ftxui::text(""),
        ftxui::vbox(std::move(visiblePathRows)) | ftxui::flex
    })
    | ftxui::border
    | ftxui::bgcolor(theme.panelBg)
    | ftxui::color(theme.border)
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

bool HelpModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) {
        return false;
    }

    // Mouse Handling
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_isCloseBtnHovered = m_closeBtnBox.Contain(mouse.x, mouse.y);
        m_isSysInfoBackBtnHovered = m_sysInfoBackBtnBox.Contain(mouse.x, mouse.y);
        m_isSysInfoBtnHovered = m_sysInfoBtnBox.Contain(mouse.x, mouse.y);

        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            if (m_isCloseBtnHovered || !m_modalBox.Contain(mouse.x, mouse.y)) {
                if (m_showSystemInfo) {
                    m_showSystemInfo = false;
                } else {
                    Hide();
                    if (OnCloseRequested) OnCloseRequested();
                }
                return true;
            }

            if (m_showSystemInfo && m_isSysInfoBackBtnHovered) {
                m_showSystemInfo = false;
                return true;
            }

            if (!m_showSystemInfo && m_isSysInfoBtnHovered) {
                m_showSystemInfo = true;
                m_sysInfoVScroll = 0;
                return true;
            }
        }

        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (m_showSystemInfo) {
                if (m_sysInfoVScroll > 0) m_sysInfoVScroll--;
            } else {
                if (m_helpScrollOffset > 0) m_helpScrollOffset--;
            }
            return true;
        } else if (mouse.button == ftxui::Mouse::WheelDown) {
            if (m_showSystemInfo) {
                m_sysInfoVScroll++;
            } else {
                m_helpScrollOffset++;
            }
            return true;
        }
        return true;
    }

    if (event == ftxui::Event::Character('I') || event == ftxui::Event::Character('i') ||
        event.character() == "Ш" || event.character() == "ш") {
        if (OnToggleBottomBarRequested) {
            OnToggleBottomBarRequested();
        } else {
            m_showBottomBar = !m_showBottomBar;
        }
        return true;
    }

    if (event == ftxui::Event::Escape) {
        if (m_showSystemInfo) {
            m_showSystemInfo = false;
            return true;
        }
        Hide();
        if (OnCloseRequested) OnCloseRequested();
        return true;
    }

    if (m_showSystemInfo) {
        if (event == ftxui::Event::Return) {
            m_showSystemInfo = false;
            return true;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
            if (m_sysInfoVScroll > 0) m_sysInfoVScroll--;
            return true;
        }
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
            m_sysInfoVScroll++;
            return true;
        }
        return true;
    }

    // Help view keyboard navigation
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k')) {
        if (m_helpScrollOffset > 0) m_helpScrollOffset--;
        return true;
    }
    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j')) {
        m_helpScrollOffset++;
        return true;
    }
    if (event == ftxui::Event::PageUp) {
        m_helpScrollOffset = std::max(0, m_helpScrollOffset - 5);
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        m_helpScrollOffset += 5;
        return true;
    }

    if (event == ftxui::Event::Tab) {
        m_isSysInfoBtnFocused = !m_isSysInfoBtnFocused;
        return true;
    }

    if (event == ftxui::Event::Return && m_isSysInfoBtnFocused) {
        m_showSystemInfo = true;
        m_sysInfoVScroll = 0;
        return true;
    }

    return true;
}

} // namespace tui
