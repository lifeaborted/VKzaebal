#pragma once

#include "ui/tui/TuiTheme.h"
#include "ui/tui/modals/IModalDialog.h"
#include "ui/tui/modals/settings/ISettingsTab.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

class ConfigurationService;

namespace tui {

class TuiThemeConfig;

class SettingsModalComponent : public IModalDialog {
public:
    SettingsModalComponent(ConfigurationService& configService, TuiThemeConfig& themeConfig);
    ~SettingsModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette) override;
    void Show();
    void Hide() override;
    bool IsVisible() const override { return m_isVisible; }

    std::function<void()> OnCloseRequested;
    std::function<void()> OnSettingsChanged;
    std::function<void()> OnToggleBottomBarRequested;

    void SetShowBottomBar(bool show) override { m_showBottomBar = show; }
    bool GetShowBottomBar() const { return m_showBottomBar; }
    bool IsTyping() const override { return m_isEditing; }

private:
    void ChangeOptionValue(int delta);
    ftxui::Element RenderCategories();
    ftxui::Element RenderOptionsList(int bodyHeight);

    ConfigurationService& m_configService;
    TuiThemeConfig& m_themeConfig;
    ThemePalette m_theme;
    bool m_isVisible = false;
    bool m_showBottomBar = true;

    std::vector<std::unique_ptr<ISettingsTab>> m_tabs;
    int m_selectedCategory = 0;
    int m_selectedOption = 0;
    int m_scrollOffset = 0;
    int m_lastBodyHeight = 20;

    // String / color edit mode
    bool m_isEditing = false;
    int m_editingCategory = -1;
    int m_editingOption = -1;
    std::string m_editingBuffer;
    int m_editCursor = 0;

    // Hit-testing boxes
    ftxui::Box m_modalBox;
    ftxui::Box m_closeBtnBox;
    std::vector<ftxui::Box> m_categoryBoxes;
    std::vector<ftxui::Box> m_optionBoxes;
    std::vector<ftxui::Box> m_optMinusBoxes;
    std::vector<ftxui::Box> m_optPlusBoxes;
    bool m_isCloseBtnHovered = false;
    int m_hoveredCategory = -1;
    int m_hoveredOption = -1;

    // Scrollbar hit-testing and dragging
    ftxui::Box m_scrollUpBox;
    ftxui::Box m_scrollDownBox;
    ftxui::Box m_scrollbarTrackBox;
    bool m_isScrollUpHovered = false;
    bool m_isScrollDownHovered = false;
    bool m_isScrollTrackHovered = false;
    bool m_isDraggingScrollbar = false;
};

} // namespace tui
