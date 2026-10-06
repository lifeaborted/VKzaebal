#pragma once

#include "ui/tui/modals/IModalDialog.h"
#include <vector>
#include <memory>
#include <functional>

namespace tui {

// ==============================================================================
// TuiModalManager
// ------------------------------------------------------------------------------
// Менеджер модальных окон в TUI интерфейсе.
// Централизованно управляет регистрацией, наложением оверлеев (dbox),
// перехватом ввода и рассылкой обновления тем для всех диалогов.
// ==============================================================================
class TuiModalManager {
public:
    TuiModalManager() = default;
    ~TuiModalManager() = default;

    void RegisterModal(std::shared_ptr<IModalDialog> modal);

    bool HasActiveModal() const;
    std::shared_ptr<IModalDialog> GetActiveModal() const;
    void CloseAll();

    void UpdateTheme(const ThemePalette& palette);
    void SetShowBottomBar(bool show);

    ftxui::Element RenderOverlay(ftxui::Element baseElement, const std::function<ftxui::Element(ftxui::Element)>& autoHideCursor);
    bool HandleEvent(ftxui::Event event);

private:
    std::vector<std::shared_ptr<IModalDialog>> m_modals;
};

} // namespace tui
