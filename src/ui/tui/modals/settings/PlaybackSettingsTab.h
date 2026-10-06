#pragma once

#include "ISettingsTab.h"

class ConfigurationService;

namespace tui {

class PlaybackSettingsTab : public ISettingsTab {
public:
    explicit PlaybackSettingsTab(ConfigurationService& configService);
    ~PlaybackSettingsTab() override = default;

    std::string GetTitle() const override { return "Воспроизведение"; }
    void LoadSettings() override;
    std::vector<SettingItem> GetItems() const override;
    std::string GetCurrentValue(int optionIndex) const override;
    void ChangeValue(int optionIndex, int delta) override;
    void SetCurrentValue(int optionIndex, const std::string& value) override;
    bool IsOptionDisabled(int optionIndex) const override;

private:
    ConfigurationService& m_configService;
    int m_savePositionMode = 2;
    bool m_autoPlay = false;
    bool m_crossfadeEnabled = false;
    int m_crossfadeMs = 3000;
    int m_repeatMode = 1;
    bool m_shuffle = false;
    bool m_gapless = true;
};

} // namespace tui
