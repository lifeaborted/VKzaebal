#pragma once

#include <string>
#include <vector>

namespace tui {

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

class ISettingsTab {
public:
    virtual ~ISettingsTab() = default;

    virtual std::string GetTitle() const = 0;
    virtual void LoadSettings() = 0;
    virtual std::vector<SettingItem> GetItems() const = 0;
    virtual std::string GetCurrentValue(int optionIndex) const = 0;
    virtual void ChangeValue(int optionIndex, int delta) = 0;
    virtual void SetCurrentValue(int optionIndex, const std::string& value) = 0;
    virtual bool IsOptionDisabled(int /*optionIndex*/) const { return false; }
};

} // namespace tui
