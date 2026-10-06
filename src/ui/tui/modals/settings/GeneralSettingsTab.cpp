#include "GeneralSettingsTab.h"
#include "services/config/ConfigurationService.h"
#include <algorithm>

namespace tui {

GeneralSettingsTab::GeneralSettingsTab(ConfigurationService& configService)
    : m_configService(configService) {
    LoadSettings();
}

void GeneralSettingsTab::LoadSettings() {
    std::string src = m_configService.GetActiveSource();
    if (src.rfind("Custom:", 0) == 0) src = src.substr(7);
    m_sourceIndex = 0;
    for (size_t i = 0; i < m_availableSources.size(); ++i) {
        if (m_availableSources[i] == src) {
            m_sourceIndex = static_cast<int>(i);
            break;
        }
    }
    m_cacheSizeMb = m_configService.GetDiskCacheSizeMb();
    if (m_cacheSizeMb <= 0) m_cacheSizeMb = 100;
    m_seekStepSec = m_configService.GetSeekStepSeconds();
    if (m_seekStepSec <= 0) m_seekStepSec = 5;
}

std::vector<SettingItem> GeneralSettingsTab::GetItems() const {
    return {
        {"Источник по умолчанию (source)", "general", "source", SettingType::CHOICE, m_availableSources, 1, 0, static_cast<float>(m_availableSources.size() - 1)},
        {"Лимит дискового кэша (кэш MB)", "network", "cache", SettingType::STEPPER, {}, 50, 50, 2000},
        {"Шаг перемотки стрелками (сек)", "playback", "seek", SettingType::STEPPER, {}, 1, 1, 60}
    };
}

std::string GeneralSettingsTab::GetCurrentValue(int optionIndex) const {
    switch (optionIndex) {
        case 0:
            if (m_sourceIndex >= 0 && m_sourceIndex < static_cast<int>(m_availableSources.size())) {
                return m_availableSources[m_sourceIndex];
            }
            return "VK";
        case 1:
            return std::to_string(m_cacheSizeMb) + " MB";
        case 2:
            return std::to_string(m_seekStepSec) + " сек";
        default:
            return "";
    }
}

void GeneralSettingsTab::ChangeValue(int optionIndex, int delta) {
    if (optionIndex == 0) {
        int n = static_cast<int>(m_availableSources.size());
        m_sourceIndex = (m_sourceIndex + delta % n + n) % n;
        m_configService.SetActiveSource(m_availableSources[m_sourceIndex]);
    } else if (optionIndex == 1) {
        m_cacheSizeMb = std::clamp(m_cacheSizeMb + delta * 50, 50, 2000);
        m_configService.SetDiskCacheSizeMb(m_cacheSizeMb);
    } else if (optionIndex == 2) {
        m_seekStepSec = std::clamp(m_seekStepSec + delta, 1, 60);
        m_configService.SetSeekStepSeconds(m_seekStepSec);
    }
}

void GeneralSettingsTab::SetCurrentValue(int optionIndex, const std::string& value) {
    if (optionIndex == 0) {
        for (size_t i = 0; i < m_availableSources.size(); ++i) {
            if (m_availableSources[i] == value) {
                m_sourceIndex = static_cast<int>(i);
                m_configService.SetActiveSource(value);
                break;
            }
        }
    }
}

} // namespace tui
