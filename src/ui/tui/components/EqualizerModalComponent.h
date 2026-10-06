#pragma once

#include "ui/tui/TuiTheme.h"
#include "core/audio/IAudioEngine.h"
#include "ui/tui/modals/IModalDialog.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tui {

struct EqPreset {
    std::string name;
    std::vector<float> gains;
};

class EqualizerModalComponent : public IModalDialog {
public:
    explicit EqualizerModalComponent(IAudioEngine& audio);
    ~EqualizerModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette) override;
    void Show();
    void Hide() override;
    bool IsVisible() const override { return m_isVisible; }

    std::function<void()> OnCloseRequested;

    void SetShowBottomBar(bool show) override { m_showBottomBar = show; }
    bool GetShowBottomBar() const { return m_showBottomBar; }
    bool IsTyping() const override { return false; }

    static const std::vector<EqPreset>& GetPresets();

private:
    ftxui::Element RenderBandColumn(int bandIdx, bool isSelected);

    IAudioEngine& m_audio;
    bool m_isVisible = false;
    bool m_showBottomBar = true;
    ThemePalette m_theme;

    int m_selectedBand = 0; // 0..9
    int m_currentPresetIndex = 0;

    void ApplyPreset(int index);
    void AdjustCurrentBand(float deltaDb);
    void SyncFromEngine();

    std::vector<ftxui::Box> m_bandBoxes{10};
    ftxui::Box m_modalBox;
    ftxui::Box m_toggleBox;
    ftxui::Box m_resetBox;
    ftxui::Box m_prevPresetBox;
    ftxui::Box m_nextPresetBox;
    ftxui::Box m_closeBox;
    bool m_isDragging = false;
    bool m_isCloseHovered = false;
    bool m_isToggleHovered = false;
    bool m_isResetHovered = false;
    bool m_isPrevPresetHovered = false;
    bool m_isNextPresetHovered = false;
    int m_hoveredBand = -1;
};

} // namespace tui
