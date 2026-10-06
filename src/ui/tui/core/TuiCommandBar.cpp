#include "ui/tui/core/TuiCommandBar.h"

namespace tui {

TuiCommandBar::TuiCommandBar() {
    ftxui::InputOption opt;
    opt.multiline = false;
    m_inputComponent = ftxui::Input(&m_inputText, "команда...", opt);
}

void TuiCommandBar::Open() {
    m_isActive = true;
    m_inputText.clear();
    if (m_inputComponent) {
        m_inputComponent->TakeFocus();
    }
}

void TuiCommandBar::Close() {
    m_isActive = false;
    m_inputText.clear();
}

ftxui::Element TuiCommandBar::Render(const ThemePalette& theme) {
    if (!m_isActive) return ftxui::emptyElement();
    return ftxui::hbox({
        ftxui::text(" / ") | ftxui::bold | ftxui::color(theme.accentCyan),
        m_inputComponent->Render() | ftxui::flex,
        ftxui::text(" [Enter: Выполнить | Esc: Закрыть] ") | ftxui::color(theme.textMuted)
    }) | ftxui::bgcolor(theme.highlight);
}

bool TuiCommandBar::OnEvent(ftxui::Event event) {
    if (!m_isActive) return false;

    if (event == ftxui::Event::Escape) {
        Close();
        if (OnDismiss) OnDismiss();
        return true;
    }

    if (event == ftxui::Event::Return) {
        std::string cmd = m_inputText;
        Close();
        if (!cmd.empty()) {
            if (cmd.front() == '/') cmd = cmd.substr(1);
            if (OnExecuteCommand) OnExecuteCommand(cmd);
        } else {
            if (OnDismiss) OnDismiss();
        }
        return true;
    }

    if (m_inputComponent) {
        return m_inputComponent->OnEvent(event);
    }
    return true;
}

} // namespace tui
