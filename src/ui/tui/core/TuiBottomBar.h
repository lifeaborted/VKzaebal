#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/dom/elements.hpp>

namespace tui {

// ==============================================================================
// TuiBottomBar
// ------------------------------------------------------------------------------
// Нижняя информационная строка подсказок горячих клавиш.
// ==============================================================================
class TuiBottomBar {
public:
    TuiBottomBar() = default;
    ~TuiBottomBar() = default;

    void Toggle() { m_showBottomBar = !m_showBottomBar; }
    void SetVisible(bool show) { m_showBottomBar = show; }
    bool IsVisible() const { return m_showBottomBar; }
    void SetIsOnline(bool online) { m_isOnline = online; }
    bool IsOnline() const { return m_isOnline; }

    ftxui::Element RenderNowPlayingBar(const ThemePalette& theme) const;
    ftxui::Element RenderSearchBar(const ThemePalette& theme) const;
    ftxui::Element RenderForScreen(int screenIndex, const ThemePalette& theme) const;

private:
    bool m_showBottomBar = true;
    bool m_isOnline = true;
};

} // namespace tui
