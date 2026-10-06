#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace tui {

// ==============================================================================
// IModalDialog
// ------------------------------------------------------------------------------
// Базовый интерфейс для всех всплывающих модальных окон в TUI.
// Наследует ftxui::ComponentBase, предоставляя методы видимости, рендеринга и стилизации.
// ==============================================================================
class IModalDialog : public ftxui::ComponentBase {
public:
    ~IModalDialog() override = default;

    virtual bool IsVisible() const = 0;
    virtual void Hide() = 0;
    virtual void UpdateTheme(const ThemePalette& palette) = 0;
    virtual void SetShowBottomBar(bool show) { (void)show; }
    virtual bool IsTyping() const { return false; }
};

} // namespace tui
