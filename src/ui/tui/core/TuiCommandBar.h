#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <functional>

namespace tui {

// ==============================================================================
// TuiCommandBar
// ------------------------------------------------------------------------------
// Нижняя интерактивная командная строка (активируется клавишей '/').
// ==============================================================================
class TuiCommandBar {
public:
    TuiCommandBar();
    ~TuiCommandBar() = default;

    void Open();
    void Close();
    bool IsActive() const { return m_isActive; }

    ftxui::Component GetComponent() { return m_inputComponent; }
    ftxui::Element Render(const ThemePalette& theme);
    bool OnEvent(ftxui::Event event);

    std::function<void(const std::string& cmd)> OnExecuteCommand;
    std::function<void()> OnDismiss;

private:
    bool m_isActive = false;
    std::string m_inputText;
    ftxui::Component m_inputComponent;
};

} // namespace tui
