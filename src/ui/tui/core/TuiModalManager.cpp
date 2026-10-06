#include "ui/tui/core/TuiModalManager.h"
#include <ftxui/dom/elements.hpp>

namespace tui {

void TuiModalManager::RegisterModal(std::shared_ptr<IModalDialog> modal) {
    if (modal) {
        m_modals.push_back(modal);
    }
}

bool TuiModalManager::HasActiveModal() const {
    for (const auto& modal : m_modals) {
        if (modal && modal->IsVisible()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<IModalDialog> TuiModalManager::GetActiveModal() const {
    for (const auto& modal : m_modals) {
        if (modal && modal->IsVisible()) {
            return modal;
        }
    }
    return nullptr;
}

void TuiModalManager::CloseAll() {
    for (auto& modal : m_modals) {
        if (modal && modal->IsVisible()) {
            modal->Hide();
        }
    }
}

void TuiModalManager::UpdateTheme(const ThemePalette& palette) {
    for (auto& modal : m_modals) {
        if (modal) {
            modal->UpdateTheme(palette);
        }
    }
}

void TuiModalManager::SetShowBottomBar(bool show) {
    for (auto& modal : m_modals) {
        if (modal) {
            modal->SetShowBottomBar(show);
        }
    }
}

ftxui::Element TuiModalManager::RenderOverlay(ftxui::Element baseElement,
                                             const std::function<ftxui::Element(ftxui::Element)>& autoHideCursor) {
    auto active = GetActiveModal();
    if (active) {
        return autoHideCursor(ftxui::dbox({
            std::move(baseElement),
            active->Render()
        }));
    }
    return autoHideCursor(std::move(baseElement));
}

bool TuiModalManager::HandleEvent(ftxui::Event event) {
    auto active = GetActiveModal();
    if (active) {
        if (event == ftxui::Event::Custom) {
            return false;
        }
        return active->OnEvent(event);
    }
    return false;
}

} // namespace tui
