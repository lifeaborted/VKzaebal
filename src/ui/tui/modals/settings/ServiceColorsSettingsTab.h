#pragma once

#include "ISettingsTab.h"

namespace tui {

class TuiThemeConfig;

class ServiceColorsSettingsTab : public ISettingsTab {
public:
    explicit ServiceColorsSettingsTab(TuiThemeConfig& themeConfig);
    ~ServiceColorsSettingsTab() override = default;

    std::string GetTitle() const override { return "Цвета сервисов"; }
    void LoadSettings() override;
    std::vector<SettingItem> GetItems() const override;
    std::string GetCurrentValue(int optionIndex) const override;
    void ChangeValue(int optionIndex, int delta) override;
    void SetCurrentValue(int optionIndex, const std::string& value) override;

private:
    TuiThemeConfig& m_themeConfig;
};

} // namespace tui
