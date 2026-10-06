#include "EqualizerModalComponent.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace tui {

static const std::vector<EqPreset> S_PRESETS = {
    {"Flat",         { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}},
    {"Bass Boost",   { 6.0f,  5.0f,  3.0f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}},
    {"Bass Reducer", {-6.0f, -5.0f, -3.0f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}},
    {"Treble Boost", { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  1.0f,  3.0f,  5.0f,  6.0f,  7.0f}},
    {"Vocal Boost",  {-2.0f, -1.0f,  0.0f,  2.0f,  4.0f,  4.0f,  2.0f,  1.0f,  0.0f, -1.0f}},
    {"Rock",         { 5.0f,  3.0f,  1.0f, -1.0f, -2.0f,  0.0f,  2.0f,  4.0f,  5.0f,  6.0f}},
    {"Pop",          {-1.0f,  1.0f,  3.0f,  4.0f,  3.0f,  0.0f, -1.0f, -1.0f,  1.0f,  2.0f}},
    {"Electronic",   { 5.0f,  4.0f,  2.0f,  0.0f, -2.0f,  1.0f,  0.0f,  2.0f,  4.0f,  5.0f}},
    {"Classical",    { 4.0f,  3.0f,  2.0f,  1.0f, -1.0f, -1.0f,  0.0f,  2.0f,  3.0f,  4.0f}}
};

static const std::vector<std::string> BAND_LABELS = {
    " 31Hz", " 62Hz", "125Hz", "250Hz", "500Hz", " 1kHz", " 2kHz", " 4kHz", " 8kHz", "16kHz"
};

const std::vector<EqPreset>& EqualizerModalComponent::GetPresets() {
    return S_PRESETS;
}

EqualizerModalComponent::EqualizerModalComponent(IAudioEngine& audio)
    : m_audio(audio) {
    SyncFromEngine();
}

void EqualizerModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void EqualizerModalComponent::Show() {
    m_isVisible = true;
    SyncFromEngine();
}

void EqualizerModalComponent::Hide() {
    m_isVisible = false;
    if (OnCloseRequested) {
        OnCloseRequested();
    }
}

void EqualizerModalComponent::SyncFromEngine() {
    std::string currentPreset = m_audio.GetEqualizerPreset();
    m_currentPresetIndex = -1;
    for (size_t i = 0; i < S_PRESETS.size(); ++i) {
        if (S_PRESETS[i].name == currentPreset) {
            m_currentPresetIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_currentPresetIndex == -1) {
        m_currentPresetIndex = 0; // Flat
    }
}

void EqualizerModalComponent::ApplyPreset(int index) {
    if (index < 0 || index >= static_cast<int>(S_PRESETS.size())) return;
    m_currentPresetIndex = index;
    const auto& preset = S_PRESETS[index];
    m_audio.SetEqualizerBands(preset.gains);
    m_audio.SetEqualizerPreset(preset.name);
    m_audio.SetEqualizerEnabled(true);
}

void EqualizerModalComponent::AdjustCurrentBand(float deltaDb) {
    float curGain = m_audio.GetEqualizerBandGain(m_selectedBand);
    float newGain = std::clamp(std::round(curGain + deltaDb), -12.0f, 12.0f);
    m_audio.SetEqualizerBandGain(m_selectedBand, newGain);
    m_audio.SetEqualizerPreset("Custom");
    m_audio.SetEqualizerEnabled(true);
}

ftxui::Element EqualizerModalComponent::RenderBandColumn(int bandIdx, bool isSelected) {
    using namespace ftxui;

    float gain = m_audio.GetEqualizerBandGain(bandIdx);
    int gainInt = static_cast<int>(std::round(gain));

    // Vertical visual gauge: 11 lines (5 chars wide)
    Elements rows;
    for (int r = 0; r < 11; ++r) {
        if (r < 5) {
            int threshold = (5 - r) * 2; // r=0: 10..12, r=1: 8..9, r=2: 6..7, r=3: 4..5, r=4: 1..3
            bool active = (gainInt >= threshold);
            std::string block = active ? "  █  " : "  │  ";
            auto elem = text(block);
            if (active) {
                elem = elem | color(isSelected ? m_theme.accent : m_theme.accentCyan) | bold;
            } else {
                elem = elem | color(m_theme.border);
            }
            rows.push_back(elem | center);
        } else if (r == 5) {
            // Center 0 dB mark
            auto elem = text("─────");
            if (gainInt == 0) {
                elem = elem | color(isSelected ? m_theme.accent : m_theme.accentCyan) | bold;
            } else {
                elem = elem | color(m_theme.borderFocus);
            }
            rows.push_back(elem | center);
        } else {
            // Below center: negative gain
            int threshold = -( (r - 5) * 2 );
            bool active = (gainInt <= threshold);
            std::string block = active ? "  █  " : "  │  ";
            auto elem = text(block);
            if (active) {
                elem = elem | color(isSelected ? m_theme.accent : m_theme.accentCyan) | bold;
            } else {
                elem = elem | color(m_theme.border);
            }
            rows.push_back(elem | center);
        }
    }

    // dB value formatting (exact 5 chars)
    std::string valStr;
    if (gainInt > 0) {
        valStr = "+" + std::to_string(gainInt) + "dB";
        if (valStr.size() == 4) valStr = " " + valStr;
    } else if (gainInt < 0) {
        valStr = std::to_string(gainInt) + "dB";
        if (valStr.size() == 4) valStr = " " + valStr;
    } else {
        valStr = "  0dB";
    }

    auto valElem = text(valStr) | center;
    if (isSelected) valElem = valElem | color(m_theme.accent) | bold;
    else valElem = valElem | color(m_theme.text);

    auto freqElem = text(BAND_LABELS[bandIdx]) | center;
    if (isSelected) freqElem = freqElem | color(m_theme.accent) | bold;
    else freqElem = freqElem | color(m_theme.textMuted);

    auto pointerElem = text(isSelected ? "  ▲  " : "     ") | center | color(m_theme.accent) | bold;

    auto columnBox = vbox({
        vbox(std::move(rows)),
        separatorEmpty(),
        valElem,
        freqElem,
        pointerElem
    });

    if (bandIdx < static_cast<int>(m_bandBoxes.size())) {
        columnBox = columnBox | reflect(m_bandBoxes[bandIdx]);
    }

    if (isSelected) {
        columnBox = columnBox | bgcolor(m_theme.activeRow);
    } else if (bandIdx == m_hoveredBand) {
        columnBox = columnBox | bgcolor(m_theme.highlight);
    }

    return columnBox;
}

ftxui::Element EqualizerModalComponent::Render() {
    using namespace ftxui;

    if (!m_isVisible) return emptyElement();

    bool isEnabled = m_audio.IsEqualizerEnabled();
    std::string presetName = m_audio.GetEqualizerPreset();

    // 1. Top bar: Title and Close button (standard matching Help/Playlist modals)
    auto closeBtn = text(" [esc: закрыть] ")
        | (m_isCloseHovered ? color(Color::White) | bgcolor(m_theme.highlight)
                            : color(m_theme.textMuted))
        | reflect(m_closeBox);

    auto topBar = hbox({
        text(" ┌─ Эквалайзер (10 полос) ") | bold | color(m_theme.accent),
        filler(),
        closeBtn,
        text(" ─┐ ") | color(m_theme.accent)
    });

    // 2. Control bar: Status, Preset switcher, Reset
    auto statusBtn = text(isEnabled ? " [● ВКЛ] " : " [○ ВЫКЛ] ")
        | bold
        | (isEnabled ? color(m_theme.accent) | bgcolor(m_theme.activeRow)
                     : color(m_theme.textMuted))
        | (m_isToggleHovered ? bgcolor(m_theme.highlight) : nothing)
        | reflect(m_toggleBox);

    auto prevBtn = text(" ◀ ")
        | bold
        | color(m_isPrevPresetHovered ? Color::White : m_theme.accentCyan)
        | (m_isPrevPresetHovered ? bgcolor(m_theme.highlight) : nothing)
        | reflect(m_prevPresetBox);

    auto nextBtn = text(" ▶ ")
        | bold
        | color(m_isNextPresetHovered ? Color::White : m_theme.accentCyan)
        | (m_isNextPresetHovered ? bgcolor(m_theme.highlight) : nothing)
        | reflect(m_nextPresetBox);

    auto presetLabel = text("Пресет: ") | color(m_theme.textMuted);
    auto presetNameElem = text(presetName) | bold | color(m_theme.accentOrange);

    auto resetBtn = text(" [Сброс 0dB] ")
        | color(m_isResetHovered ? Color::White : m_theme.textMuted)
        | (m_isResetHovered ? bgcolor(m_theme.highlight) : nothing)
        | reflect(m_resetBox);

    auto controlBar = hbox({
        text("   Состояние: ") | color(m_theme.textMuted),
        statusBtn,
        filler(),
        presetLabel,
        prevBtn,
        presetNameElem,
        nextBtn,
        filler(),
        resetBtn,
        text("   ")
    });

    // 3. Scale and 10 Band columns
    Elements scaleElements;
    scaleElements.push_back(text("+12dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" +9dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" +6dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" +3dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" +1dB") | color(m_theme.textMuted));
    scaleElements.push_back(text("  0dB") | color(m_theme.accent) | bold);
    scaleElements.push_back(text(" -1dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" -3dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" -6dB") | color(m_theme.textMuted));
    scaleElements.push_back(text(" -9dB") | color(m_theme.textMuted));
    scaleElements.push_back(text("-12dB") | color(m_theme.textMuted));
    scaleElements.push_back(separatorEmpty());
    scaleElements.push_back(text("Усил:") | color(m_theme.textMuted));
    scaleElements.push_back(text("Част:") | color(m_theme.textMuted));
    scaleElements.push_back(text("     "));

    Elements bandColumns;
    for (int b = 0; b < 10; ++b) {
        bandColumns.push_back(RenderBandColumn(b, b == m_selectedBand));
        if (b < 9) {
            bandColumns.push_back(text(" "));
        }
    }

    auto bandsBox = hbox({
        filler(),
        vbox(std::move(scaleElements)),
        text(" "),
        separatorLight() | color(m_theme.border),
        text(" "),
        hbox(std::move(bandColumns)),
        filler()
    });

    // 4. Footer navigation hints (clean 74 chars, fits in 78 width without truncating)
    auto footerBox = hbox({
        filler(),
        text(" [←/→] Полоса   [↑/↓/Колесо] Уровень (±1 dB)   [Enter] Вкл/Выкл   [[ / ]] Пресет   [Esc] Закрыть ")
            | color(m_theme.textMuted),
        filler()
    });

    int termHeight = ftxui::Terminal::Size().dimy;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (winH > 10) termHeight = winH;
    }
#endif
    int modalWidth = 78;
    int targetHeight = m_showBottomBar ? 25 : 23;
    int modalHeight = std::clamp(termHeight - 2, 21, targetHeight);

    std::vector<Element> modalChildren;
    modalChildren.push_back(topBar);
    modalChildren.push_back(separatorLight() | color(m_theme.border));
    modalChildren.push_back(controlBar);
    modalChildren.push_back(separatorLight() | color(m_theme.border));
    modalChildren.push_back(separatorEmpty());
    modalChildren.push_back(bandsBox | flex);
    modalChildren.push_back(separatorEmpty());
    if (m_showBottomBar) {
        modalChildren.push_back(separatorLight() | color(m_theme.border));
        modalChildren.push_back(footerBox);
    }

    auto modalWindow = vbox(std::move(modalChildren))
    | borderRounded
    | color(m_theme.border)
    | bgcolor(m_theme.panelBg)
    | size(WIDTH, EQUAL, modalWidth)
    | size(HEIGHT, EQUAL, modalHeight)
    | clear_under
    | reflect(m_modalBox);

    return vbox({
        filler(),
        hbox({
            filler(),
            modalWindow,
            filler()
        }),
        filler()
    });
}

bool EqualizerModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) return false;

    // --- Обработка событий мыши ---
    if (event.is_mouse()) {
        auto mouse = event.mouse();

        // Обновление состояний наведения (hover)
        m_isCloseHovered = m_closeBox.Contain(mouse.x, mouse.y);
        m_isToggleHovered = m_toggleBox.Contain(mouse.x, mouse.y);
        m_isResetHovered = m_resetBox.Contain(mouse.x, mouse.y);
        m_isPrevPresetHovered = m_prevPresetBox.Contain(mouse.x, mouse.y);
        m_isNextPresetHovered = m_nextPresetBox.Contain(mouse.x, mouse.y);

        m_hoveredBand = -1;
        for (int b = 0; b < 10; ++b) {
            if (b < static_cast<int>(m_bandBoxes.size()) && m_bandBoxes[b].Contain(mouse.x, mouse.y)) {
                m_hoveredBand = b;
                break;
            }
        }

        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Released) {
            m_isDragging = false;
        }

        // 1. Кнопка закрытия [esc: закрыть]
        if (m_isCloseHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            Hide();
            return true;
        }

        // 2. Кнопка ВКЛ/ВЫКЛ
        if (m_isToggleHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            m_audio.SetEqualizerEnabled(!m_audio.IsEqualizerEnabled());
            return true;
        }

        // 3. Кнопка сброса Flat
        if (m_isResetHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            ApplyPreset(0);
            return true;
        }

        // 4. Переключение пресетов ◀ и ▶
        if (m_isPrevPresetHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            int next = (m_currentPresetIndex + static_cast<int>(S_PRESETS.size()) - 1) % static_cast<int>(S_PRESETS.size());
            ApplyPreset(next);
            return true;
        }
        if (m_isNextPresetHovered && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            int next = (m_currentPresetIndex + 1) % static_cast<int>(S_PRESETS.size());
            ApplyPreset(next);
            return true;
        }

        // 5. Взаимодействие с полосами (Клик, Перетаскивание, Колесо мыши)
        for (int b = 0; b < 10; ++b) {
            if (b < static_cast<int>(m_bandBoxes.size()) && m_bandBoxes[b].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::WheelUp) {
                    m_selectedBand = b;
                    AdjustCurrentBand(1.0f);
                    return true;
                }
                if (mouse.button == ftxui::Mouse::WheelDown) {
                    m_selectedBand = b;
                    AdjustCurrentBand(-1.0f);
                    return true;
                }

                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    m_selectedBand = b;
                    m_isDragging = true;
                    int rowY = mouse.y - m_bandBoxes[b].y_min;
                    if (rowY >= 0 && rowY <= 10) {
                        float targetGain = std::clamp(std::round(12.0f - rowY * 2.4f), -12.0f, 12.0f);
                        m_audio.SetEqualizerBandGain(b, targetGain);
                        m_audio.SetEqualizerPreset("Custom");
                        m_audio.SetEqualizerEnabled(true);
                    }
                    return true;
                }
            }
        }

        // 6. Плавное перетаскивание при движении мыши с зажатой ЛКМ
        if (m_isDragging && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            for (int b = 0; b < 10; ++b) {
                if (b < static_cast<int>(m_bandBoxes.size()) &&
                    mouse.x >= m_bandBoxes[b].x_min && mouse.x <= m_bandBoxes[b].x_max) {
                    m_selectedBand = b;
                    int rowY = mouse.y - m_bandBoxes[b].y_min;
                    int targetRow = std::clamp(rowY, 0, 10);
                    float targetGain = std::clamp(std::round(12.0f - targetRow * 2.4f), -12.0f, 12.0f);
                    m_audio.SetEqualizerBandGain(b, targetGain);
                    m_audio.SetEqualizerPreset("Custom");
                    m_audio.SetEqualizerEnabled(true);
                    return true;
                }
            }
        }

        return true;
    }

    // --- Клавиатурная навигация ---
    // Close on Escape or E/e or У/у
    if (event == ftxui::Event::Escape ||
        event == ftxui::Event::Character('e') || event == ftxui::Event::Character('E') ||
        event.character() == "у" || event.character() == "У") {
        Hide();
        return true;
    }

    // Toggle on/off on Enter
    if (event == ftxui::Event::Return) {
        m_audio.SetEqualizerEnabled(!m_audio.IsEqualizerEnabled());
        return true;
    }

    // Band navigation: Left / Right
    if (event == ftxui::Event::ArrowLeft) {
        m_selectedBand = (m_selectedBand + 9) % 10;
        return true;
    }
    if (event == ftxui::Event::ArrowRight) {
        m_selectedBand = (m_selectedBand + 1) % 10;
        return true;
    }

    // Gain adjustment: Up / Down (±1 dB)
    if (event == ftxui::Event::ArrowUp) {
        AdjustCurrentBand(1.0f);
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        AdjustCurrentBand(-1.0f);
        return true;
    }

    // Preset navigation: [ and ]
    if (event == ftxui::Event::Character('[')) {
        int nextPreset = (m_currentPresetIndex + static_cast<int>(S_PRESETS.size()) - 1) % static_cast<int>(S_PRESETS.size());
        ApplyPreset(nextPreset);
        return true;
    }
    if (event == ftxui::Event::Character(']')) {
        int nextPreset = (m_currentPresetIndex + 1) % static_cast<int>(S_PRESETS.size());
        ApplyPreset(nextPreset);
        return true;
    }

    // Reset to Flat on R/r or К/к
    if (event == ftxui::Event::Character('r') || event == ftxui::Event::Character('R') ||
        event.character() == "к" || event.character() == "К") {
        ApplyPreset(0); // Flat
        return true;
    }

    return true; // Eat events while modal is open
}

} // namespace tui
