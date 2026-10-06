#pragma once

#include "ISettingsTab.h"

namespace tui {

class TuiThemeConfig;

class ThemeSettingsTab : public ISettingsTab {
public:
    explicit ThemeSettingsTab(TuiThemeConfig& themeConfig);
    ~ThemeSettingsTab() override = default;

    std::string GetTitle() const override { return "Фон и тема"; }
    void LoadSettings() override;
    std::vector<SettingItem> GetItems() const override;
    std::string GetCurrentValue(int optionIndex) const override;
    void ChangeValue(int optionIndex, int delta) override;
    void SetCurrentValue(int optionIndex, const std::string& value) override;

private:
    TuiThemeConfig& m_themeConfig;
};

} // namespace tui
