#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tui {

struct HelpItem {
    std::string key;
    std::string desc;
};

struct HelpSection {
    std::string title;
    std::vector<HelpItem> items;
};

struct SystemPathInfo {
    std::string label;
    std::string path;
};

class HelpModalComponent : public ftxui::ComponentBase {
public:
    HelpModalComponent();
    ~HelpModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette);
    void Show(bool showSystemInfo = false);
    void Hide();
    bool IsVisible() const { return m_isVisible; }

    std::function<void()> OnCloseRequested;
    std::function<void()> OnToggleBottomBarRequested;

    void SetShowBottomBar(bool show) { m_showBottomBar = show; }
    bool GetShowBottomBar() const { return m_showBottomBar; }

    // Dynamic data getters (never hardcoded in UI layout)
    static std::vector<HelpSection> GetHelpSections();
    static std::vector<SystemPathInfo> GetSystemPaths();

private:
    ftxui::Element RenderHelpView();
    ftxui::Element RenderSystemInfoView();

    bool m_isVisible = false;
    bool m_showSystemInfo = false;
    bool m_showBottomBar = true;
    ThemePalette m_theme;

    // Interactive hit-test boxes
    ftxui::Box m_modalBox;
    ftxui::Box m_closeBtnBox;
    ftxui::Box m_sysInfoBtnBox;
    ftxui::Box m_sysInfoBackBtnBox;

    bool m_isCloseBtnHovered = false;
    bool m_isSysInfoBtnHovered = false;
    bool m_isSysInfoBackBtnHovered = false;
    bool m_isSysInfoBtnFocused = false;

    int m_helpScrollOffset = 0;
    int m_animTick = 0;
    int m_sysInfoVScroll = 0;
};

} // namespace tui
