#include "VisualizerSettingsTab.h"
#include "ui/tui/TuiThemeConfig.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tui {

VisualizerSettingsTab::VisualizerSettingsTab(TuiThemeConfig& themeConfig)
    : m_themeConfig(themeConfig) {
}

void VisualizerSettingsTab::LoadSettings() {
}

std::vector<SettingItem> VisualizerSettingsTab::GetItems() const {
    return {
        {"Стиль визуализатора (mode)", "visualizer", "mode", SettingType::CHOICE, {"0: CAVA Эквалайзер", "1: Симметричная волна", "2: Шелковая волна"}, 1, 0, 2},
        {"Ширина столбиков (bar_width)", "visualizer", "bar_width", SettingType::CHOICE, {"1 знакоместо", "2 знакоместа"}, 1, 1, 2},
        {"Интервал между столбиками (bar_spacing)", "visualizer", "bar_spacing", SettingType::CHOICE, {"0: Вплотную", "1: Через пробел"}, 1, 0, 1},
        {"Плавность затухания (smoothing)", "visualizer", "smoothing", SettingType::STEPPER, {}, 0.02f, 0.10f, 0.98f},
        {"Чувствительность Gain (sensitivity)", "visualizer", "sensitivity", SettingType::STEPPER, {}, 0.05f, 0.20f, 5.00f},
        {"Парящие пиковые шапки (show_peaks)", "visualizer", "show_peaks", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Цвет пиковых шапок (color_peak)", "visualizer", "color_peak", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Градиент громкости столбиков (gradient)", "visualizer", "gradient", SettingType::GRADIENT_INPUT, {}, 0, 0, 0}
    };
}

std::string VisualizerSettingsTab::GetCurrentValue(int optionIndex) const {
    auto items = GetItems();
    if (optionIndex < 0 || optionIndex >= static_cast<int>(items.size())) return "";
    const auto& item = items[optionIndex];
    std::string val = m_themeConfig.GetRawValue(item.section, item.key);

    if (item.type == SettingType::CHOICE) {
        if (item.key == "mode") {
            int m = 0;
            try { m = std::stoi(val); } catch (...) {}
            if (m >= 0 && m < static_cast<int>(item.choices.size())) return item.choices[m];
            return item.choices[0];
        } else if (item.key == "bar_width") {
            int w = 1;
            try { w = std::stoi(val); } catch (...) {}
            return (w == 2) ? item.choices[1] : item.choices[0];
        } else if (item.key == "bar_spacing") {
            int s = 1;
            try { s = std::stoi(val); } catch (...) {}
            return (s == 0) ? item.choices[0] : item.choices[1];
        }
    }
    return val;
}

void VisualizerSettingsTab::ChangeValue(int optionIndex, int delta) {
    auto items = GetItems();
    if (optionIndex < 0 || optionIndex >= static_cast<int>(items.size())) return;
    const auto& item = items[optionIndex];
    std::string curVal = m_themeConfig.GetRawValue(item.section, item.key);

    if (item.type == SettingType::CHOICE) {
        if (item.key == "mode") {
            int modeVal = 0;
            try { modeVal = std::stoi(curVal); } catch (...) {}
            modeVal = (modeVal + delta % 3 + 3) % 3;
            m_themeConfig.SetRawValue(item.section, item.key, std::to_string(modeVal));
        } else if (item.key == "bar_width") {
            int wVal = 1;
            try { wVal = std::stoi(curVal); } catch (...) {}
            wVal = (wVal == 1) ? 2 : 1;
            m_themeConfig.SetRawValue(item.section, item.key, std::to_string(wVal));
        } else if (item.key == "bar_spacing") {
            int spVal = 1;
            try { spVal = std::stoi(curVal); } catch (...) {}
            spVal = (spVal == 0) ? 1 : 0;
            m_themeConfig.SetRawValue(item.section, item.key, std::to_string(spVal));
        }
    } else if (item.type == SettingType::TOGGLE) {
        bool isTrue = (curVal == "true" || curVal == "1" || curVal == "yes" || curVal == "on");
        m_themeConfig.SetRawValue(item.section, item.key, isTrue ? "false" : "true");
    } else if (item.type == SettingType::STEPPER) {
        float fVal = 0.0f;
        try { fVal = std::stof(curVal); } catch (...) {}
        fVal += delta * item.step;
        fVal = std::clamp(fVal, item.minVal, item.maxVal);
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << fVal;
        m_themeConfig.SetRawValue(item.section, item.key, ss.str());
    }
}

void VisualizerSettingsTab::SetCurrentValue(int optionIndex, const std::string& value) {
    auto items = GetItems();
    if (optionIndex >= 0 && optionIndex < static_cast<int>(items.size())) {
        m_themeConfig.SetRawValue(items[optionIndex].section, items[optionIndex].key, value);
    }
}

} // namespace tui
