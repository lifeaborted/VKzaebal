#include "ui/tui/core/TuiBottomBar.h"

namespace tui {

ftxui::Element TuiBottomBar::RenderNowPlayingBar(const ThemePalette& theme) const {
    return ftxui::hbox({
        ftxui::text(" [Space]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Пауза  ") | ftxui::color(theme.textMuted),
        ftxui::text("[N/B]") | ftxui::bold | ftxui::color(theme.accentPurple),
        ftxui::text(" След/Пред  ") | ftxui::color(theme.textMuted),
        ftxui::text("[↑/↓]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Громкость  ") | ftxui::color(theme.textMuted),
        ftxui::text("[←/→]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Перемотка  ") | ftxui::color(theme.textMuted),
        ftxui::text("[F]") | ftxui::bold | ftxui::color(theme.accentOrange),
        ftxui::text(" Поиск  ") | ftxui::color(theme.textMuted),
        ftxui::text("[/]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Команда  ") | ftxui::color(theme.textMuted),
        ftxui::text("[L]") | ftxui::bold | ftxui::color(theme.accentRed),
        ftxui::text(" Лайк  ") | ftxui::color(theme.textMuted),
        ftxui::text("[V]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Стиль EQ  ") | ftxui::color(theme.textMuted),
        ftxui::text("[?]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Справка  ") | ftxui::color(theme.textMuted),
        ftxui::text("[[]") | ftxui::bold | ftxui::color(theme.accentOrange),
        ftxui::text(" Панель  ") | ftxui::color(theme.textMuted),
        ftxui::text("[Shift+I]") | ftxui::bold | ftxui::color(theme.accentOrange),
        ftxui::text(" Скрыть  ") | ftxui::color(theme.textMuted),
        ftxui::filler(),
        (!m_isOnline ? (ftxui::text("[OFFLINE]  ") | ftxui::bold | ftxui::color(theme.accentRed)) : ftxui::emptyElement()),
        ftxui::text("[Q] Выход ") | ftxui::color(theme.textMuted)
    });
}

ftxui::Element TuiBottomBar::RenderSearchBar(const ThemePalette& theme) const {
    return ftxui::hbox({
        ftxui::text(" [Enter]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Играть  ") | ftxui::color(theme.textMuted),
        ftxui::text("[Space]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Пауза  ") | ftxui::color(theme.textMuted),
        ftxui::text("[Tab]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Режим поиска  ") | ftxui::color(theme.textMuted),
        ftxui::text("[↑/↓]") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" Громкость  ") | ftxui::color(theme.textMuted),
        ftxui::text("[←/→]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Перемотка  ") | ftxui::color(theme.textMuted),
        ftxui::text("[J/K]") | ftxui::bold | ftxui::color(theme.accentPurple),
        ftxui::text(" Выбор  ") | ftxui::color(theme.textMuted),
        ftxui::text("[Esc]") | ftxui::bold | ftxui::color(theme.text),
        ftxui::text(" Назад  ") | ftxui::color(theme.textMuted),
        ftxui::text("[L]") | ftxui::bold | ftxui::color(theme.accentRed),
        ftxui::text(" Лайк  ") | ftxui::color(theme.textMuted),
        ftxui::text("[+]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" В очередь  ") | ftxui::color(theme.textMuted),
        ftxui::text("[?]") | ftxui::bold | ftxui::color(theme.accentCyan),
        ftxui::text(" Справка  ") | ftxui::color(theme.textMuted),
        ftxui::text("[[]") | ftxui::bold | ftxui::color(theme.accentOrange),
        ftxui::text(" Панель  ") | ftxui::color(theme.textMuted),
        ftxui::text("[Shift+I]") | ftxui::bold | ftxui::color(theme.accentOrange),
        ftxui::text(" Скрыть  ") | ftxui::color(theme.textMuted),
        ftxui::filler(),
        (!m_isOnline ? (ftxui::text("[OFFLINE]  ") | ftxui::bold | ftxui::color(theme.accentRed)) : ftxui::emptyElement()),
        ftxui::text("[Q] Выход ") | ftxui::color(theme.textMuted)
    });
}

ftxui::Element TuiBottomBar::RenderForScreen(int screenIndex, const ThemePalette& theme) const {
    if (!m_showBottomBar) return ftxui::emptyElement();
    return (screenIndex == 0) ? RenderNowPlayingBar(theme) : RenderSearchBar(theme);
}

} // namespace tui
