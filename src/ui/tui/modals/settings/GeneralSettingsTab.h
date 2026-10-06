#pragma once

#include "ISettingsTab.h"

class ConfigurationService;

namespace tui {

class GeneralSettingsTab : public ISettingsTab {
public:
    explicit GeneralSettingsTab(ConfigurationService& configService);
    ~GeneralSettingsTab() override = default;

    std::string GetTitle() const override { return "Общие"; }
    void LoadSettings() override;
    std::vector<SettingItem> GetItems() const override;
    std::string GetCurrentValue(int optionIndex) const override;
    void ChangeValue(int optionIndex, int delta) override;
    void SetCurrentValue(int optionIndex, const std::string& value) override;

private:
    ConfigurationService& m_configService;
    std::vector<std::string> m_availableSources = {"VK", "Yandex", "SoundCloud", "YouTube", "All", "Offline"};
    int m_sourceIndex = 0;
    int m_cacheSizeMb = 100;
    int m_seekStepSec = 5;
};

} // namespace tui
