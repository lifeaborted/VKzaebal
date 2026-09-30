#pragma once

#include "ui/tui/TuiTheme.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>
#include <vector>
#include <functional>

class ConfigurationService;

namespace tui {

class TuiThemeConfig;

enum class SettingType {
    CHOICE,
    TOGGLE,
    STEPPER,
    COLOR_INPUT,
    GRADIENT_INPUT
};

struct SettingItem {
    std::string label;
    std::string section;
    std::string key;
    SettingType type;
    std::vector<std::string> choices;
    float step = 1.0f;
    float minVal = 0.0f;
    float maxVal = 100.0f;
};

class SettingsModalComponent : public ftxui::ComponentBase {
public:
    SettingsModalComponent(ConfigurationService& configService, TuiThemeConfig& themeConfig);
    ~SettingsModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette);
    void Show();
    void Hide();
    bool IsVisible() const { return m_isVisible; }

    std::function<void()> OnCloseRequested;
    std::function<void()> OnSettingsChanged;
    std::function<void()> OnToggleBottomBarRequested;

    void SetShowBottomBar(bool show) { m_showBottomBar = show; }
    bool GetShowBottomBar() const { return m_showBottomBar; }

private:
    void LoadCurrentSettings();
    void ChangeOptionValue(int delta);
    int GetOptionCountForCategory(int catIdx) const;
    std::vector<SettingItem> GetItemsForCategory(int catIdx) const;

    ftxui::Element RenderCategories();
    ftxui::Element RenderOptionsList(int bodyHeight);

    ConfigurationService& m_configService;
    TuiThemeConfig& m_themeConfig;
    ThemePalette m_theme;
    bool m_isVisible = false;
    bool m_showBottomBar = true;

    int m_selectedCategory = 0; // 0: Общие, 1: Воспроизведение, 2: Визуализатор, 3: Фон и тема, 4: Цвета сервисов
    int m_selectedOption = 0;
    int m_scrollOffset = 0;
    int m_lastBodyHeight = 20;

    // String / color edit mode
    bool m_isEditing = false;
    int m_editingCategory = -1;
    int m_editingOption = -1;
    std::string m_editingBuffer;

    // Cache of General options
    int m_sourceIndex = 0;
    std::vector<std::string> m_availableSources = {"VK", "Yandex", "SoundCloud", "YouTube", "All", "Offline"};
    int m_cacheSizeMb = 100;
    int m_seekStepSec = 5;

    // Cache of Playback options
    int m_savePositionMode = 2; // 0=Не сохранять, 1=Только трек, 2=Позиция и трек
    bool m_autoPlay = false;
    bool m_crossfadeEnabled = false;
    int m_crossfadeMs = 3000;
    int m_repeatMode = 1;       // 0=Без повтора, 1=Все, 2=Один
    bool m_shuffle = false;
    bool m_gapless = true;

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
