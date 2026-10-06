#include "ThemeSettingsTab.h"
#include "ui/tui/TuiThemeConfig.h"

namespace tui {

ThemeSettingsTab::ThemeSettingsTab(TuiThemeConfig& themeConfig)
    : m_themeConfig(themeConfig) {
}

void ThemeSettingsTab::LoadSettings() {
}

std::vector<SettingItem> ThemeSettingsTab::GetItems() const {
    return {
        {"Режим фона окна (mode)", "background", "mode", SettingType::CHOICE, {"mono", "gradient"}, 1, 0, 1},
        {"Цвет моно-фона (color)", "background", "color", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Вертикальный градиент фона (gradient)", "background", "gradient", SettingType::GRADIENT_INPUT, {}, 0, 0, 0},
        {"Фон боковой панели (panel_bg)", "theme", "panel_bg", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Фон карточек и блоков (card_bg)", "theme", "card_bg", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Цвет линий и рамок (border)", "theme", "border", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Цвет рамок в фокусе (border_focus)", "theme", "border_focus", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Основной текст (text)", "theme", "text", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Приглушенный текст (text_muted)", "theme", "text_muted", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Главный акцент (accent)", "theme", "accent", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Акцент Cyan (accent_cyan)", "theme", "accent_cyan", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Акцент Orange (accent_orange)", "theme", "accent_orange", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Акцент Purple (accent_purple)", "theme", "accent_purple", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Акцент Red (accent_red)", "theme", "accent_red", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Подсветка строки курсора (highlight)", "theme", "highlight", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Подсветка играющего трека (active_row)", "theme", "active_row", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Прогресс: сыграно (progress_played)", "theme", "progress_played", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Прогресс: ползунок (progress_thumb)", "theme", "progress_thumb", SettingType::COLOR_INPUT, {}, 0, 0, 0},
        {"Прогресс: остаток (progress_remaining)", "theme", "progress_remaining", SettingType::COLOR_INPUT, {}, 0, 0, 0}
    };
}

std::string ThemeSettingsTab::GetCurrentValue(int optionIndex) const {
    auto items = GetItems();
    if (optionIndex < 0 || optionIndex >= static_cast<int>(items.size())) return "";
    return m_themeConfig.GetRawValue(items[optionIndex].section, items[optionIndex].key);
}

void ThemeSettingsTab::ChangeValue(int optionIndex, int) {
    auto items = GetItems();
    if (optionIndex < 0 || optionIndex >= static_cast<int>(items.size())) return;
    const auto& item = items[optionIndex];
    if (item.section == "background" && item.key == "mode") {
        std::string curVal = m_themeConfig.GetRawValue(item.section, item.key);
        std::string nextMode = (curVal == "gradient") ? "mono" : "gradient";
        m_themeConfig.SetRawValue(item.section, item.key, nextMode);
    }
}

void ThemeSettingsTab::SetCurrentValue(int optionIndex, const std::string& value) {
    auto items = GetItems();
    if (optionIndex >= 0 && optionIndex < static_cast<int>(items.size())) {
        m_themeConfig.SetRawValue(items[optionIndex].section, items[optionIndex].key, value);
    }
}

} // namespace tui
