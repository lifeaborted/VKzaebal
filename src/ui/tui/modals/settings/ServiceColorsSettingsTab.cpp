#include "ServiceColorsSettingsTab.h"
#include "ui/tui/TuiThemeConfig.h"

namespace tui {

ServiceColorsSettingsTab::ServiceColorsSettingsTab(TuiThemeConfig& themeConfig)
    : m_themeConfig(themeConfig) {
}

void ServiceColorsSettingsTab::LoadSettings() {
}

std::vector<SettingItem> ServiceColorsSettingsTab::GetItems() const {
    return {
        {"ВКонтакте (vk)", "services", "vk", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Яндекс Музыка (yandex)", "services", "yandex", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"SoundCloud (soundcloud)", "services", "soundcloud", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"YouTube (youtube)", "services", "youtube", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Spotify (spotify)", "services", "spotify", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Оффлайн треки (offline)", "services", "offline", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Все источники (all)", "services", "all", SettingType::COLOR_INPUT, {}, 0, 0, 0}
    };
}

std::string ServiceColorsSettingsTab::GetCurrentValue(int optionIndex) const {
    auto items = GetItems();
    if (optionIndex < 0 || optionIndex >= static_cast<int>(items.size())) return "";
    return m_themeConfig.GetRawValue(items[optionIndex].section, items[optionIndex].key);
}

void ServiceColorsSettingsTab::ChangeValue(int, int) {
}

void ServiceColorsSettingsTab::SetCurrentValue(int optionIndex, const std::string& value) {
    auto items = GetItems();
    if (optionIndex >= 0 && optionIndex < static_cast<int>(items.size())) {
        m_themeConfig.SetRawValue(items[optionIndex].section, items[optionIndex].key, value);
    }
}

} // namespace tui
