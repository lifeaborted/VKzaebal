#include "SettingsModalComponent.h"
#include "services/config/ConfigurationService.h"
#include "ui/tui/TuiThemeConfig.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace tui {

SettingsModalComponent::SettingsModalComponent(ConfigurationService& configService, TuiThemeConfig& themeConfig)
    : m_configService(configService),
      m_themeConfig(themeConfig),
      m_theme(GetDefaultTheme()) {
    LoadCurrentSettings();
}

void SettingsModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void SettingsModalComponent::Show() {
    m_isVisible = true;
    m_isEditing = false;
    m_isDraggingScrollbar = false;
    m_selectedOption = 0;
    m_scrollOffset = 0;
    LoadCurrentSettings();
}

void SettingsModalComponent::Hide() {
    m_isVisible = false;
    m_isEditing = false;
    m_isDraggingScrollbar = false;
}

void SettingsModalComponent::LoadCurrentSettings() {
    // 0. Общие
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

    // 1. Воспроизведение
    m_savePositionMode = m_configService.GetSavePositionMode();
    m_autoPlay = m_configService.GetAutoPlay();
    m_crossfadeEnabled = m_configService.GetCrossfadeEnabled();
    m_crossfadeMs = m_configService.GetCrossfadeDurationMs();
    if (m_crossfadeMs <= 0) m_crossfadeMs = 3000;
    m_repeatMode = m_configService.GetRepeatMode();
    m_shuffle = m_configService.GetShuffle();
    m_gapless = m_configService.GetGaplessPlayback();
}

int SettingsModalComponent::GetOptionCountForCategory(int catIdx) const {
    return static_cast<int>(GetItemsForCategory(catIdx).size());
}

std::vector<SettingItem> SettingsModalComponent::GetItemsForCategory(int catIdx) const {
    std::vector<SettingItem> items;
    switch (catIdx) {
        case 0: // Общие
            items.push_back({"Источник по умолчанию (source)", "general", "source", SettingType::CHOICE, m_availableSources, 1, 0, static_cast<float>(m_availableSources.size() - 1)});
            items.push_back({"Лимит дискового кэша (кэш MB)", "network", "cache", SettingType::STEPPER, {}, 50, 50, 2000});
            items.push_back({"Шаг перемотки стрелками (сек)", "playback", "seek", SettingType::STEPPER, {}, 1, 1, 60});
            break;

        case 1: // Воспроизведение
            items.push_back({"Сохранение позиции (save_pos)", "playback", "save_pos", SettingType::CHOICE, {"Не сохранять", "Только текущий трек", "Позиция и трек"}, 1, 0, 2});
            items.push_back({"Автовоспроизведение (autoplay)", "session", "autoplay", SettingType::TOGGLE, {}, 1, 0, 1});
            items.push_back({"Плавный переход (crossfade)", "audio", "crossfade", SettingType::TOGGLE, {}, 1, 0, 1});
            items.push_back({"Длительность кроссфейда (мс)", "audio", "crossfade_ms", SettingType::STEPPER, {}, 500, 500, 10000});
            items.push_back({"Режим повтора треков (repeat)", "session", "repeat", SettingType::CHOICE, {"Без повтора", "Повторять все", "Повторять один"}, 1, 0, 2});
            items.push_back({"Случайный порядок (shuffle)", "session", "shuffle", SettingType::TOGGLE, {}, 1, 0, 1});
            items.push_back({"Непрерывный переход (gapless)", "audio", "gapless", SettingType::TOGGLE, {}, 1, 0, 1});
            break;

        case 2: // Визуализатор
            items.push_back({"Стиль визуализатора (mode)", "visualizer", "mode", SettingType::CHOICE, {"0: CAVA Эквалайзер", "1: Симметричная волна", "2: Шелковая волна"}, 1, 0, 2});
            items.push_back({"Ширина столбиков (bar_width)", "visualizer", "bar_width", SettingType::CHOICE, {"1 знакоместо", "2 знакоместа"}, 1, 1, 2});
            items.push_back({"Интервал между столбиками (bar_spacing)", "visualizer", "bar_spacing", SettingType::CHOICE, {"0: Вплотную", "1: Через пробел"}, 1, 0, 1});
            items.push_back({"Плавность затухания (smoothing)", "visualizer", "smoothing", SettingType::STEPPER, {}, 0.02f, 0.10f, 0.98f});
            items.push_back({"Чувствительность Gain (sensitivity)", "visualizer", "sensitivity", SettingType::STEPPER, {}, 0.05f, 0.20f, 5.00f});
            items.push_back({"Парящие пиковые шапки (show_peaks)", "visualizer", "show_peaks", SettingType::TOGGLE, {}, 1, 0, 1});
            items.push_back({"Цвет пиковых шапок (color_peak)", "visualizer", "color_peak", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Градиент громкости столбиков (gradient)", "visualizer", "gradient", SettingType::GRADIENT_INPUT, {}, 0, 0, 0});
            break;

        case 3: // Фон и тема
            items.push_back({"Режим фона окна (mode)", "background", "mode", SettingType::CHOICE, {"mono", "gradient"}, 1, 0, 1});
            items.push_back({"Цвет моно-фона (color)", "background", "color", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Вертикальный градиент фона (gradient)", "background", "gradient", SettingType::GRADIENT_INPUT, {}, 0, 0, 0});
            items.push_back({"Фон боковой панели (panel_bg)", "theme", "panel_bg", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Фон карточек и блоков (card_bg)", "theme", "card_bg", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Цвет линий и рамок (border)", "theme", "border", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Цвет рамок в фокусе (border_focus)", "theme", "border_focus", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Основной текст (text)", "theme", "text", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Приглушенный текст (text_muted)", "theme", "text_muted", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Главный акцент (accent)", "theme", "accent", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Акцент Cyan (accent_cyan)", "theme", "accent_cyan", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Акцент Orange (accent_orange)", "theme", "accent_orange", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Акцент Purple (accent_purple)", "theme", "accent_purple", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Акцент Red (accent_red)", "theme", "accent_red", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Подсветка строки курсора (highlight)", "theme", "highlight", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Подсветка играющего трека (active_row)", "theme", "active_row", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Прогресс: сыграно (progress_played)", "theme", "progress_played", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Прогресс: ползунок (progress_thumb)", "theme", "progress_thumb", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Прогресс: остаток (progress_remaining)", "theme", "progress_remaining", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            break;

        case 4: // Цвета сервисов
            items.push_back({"ВКонтакте (vk)", "services", "vk", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Яндекс Музыка (yandex)", "services", "yandex", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"SoundCloud (soundcloud)", "services", "soundcloud", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"YouTube (youtube)", "services", "youtube", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Spotify (spotify)", "services", "spotify", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Оффлайн треки (offline)", "services", "offline", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            items.push_back({"Все источники (all)", "services", "all", SettingType::COLOR_INPUT, {}, 0, 0, 0});
            break;
    }
    return items;
}

void SettingsModalComponent::ChangeOptionValue(int delta) {
    if (m_selectedCategory == 0) {
        // Общие
        if (m_selectedOption == 0) {
            int n = static_cast<int>(m_availableSources.size());
            m_sourceIndex = (m_sourceIndex + delta % n + n) % n;
            m_configService.SetActiveSource(m_availableSources[m_sourceIndex]);
        } else if (m_selectedOption == 1) {
            m_cacheSizeMb = std::clamp(m_cacheSizeMb + delta * 50, 50, 2000);
            m_configService.SetDiskCacheSizeMb(m_cacheSizeMb);
        } else if (m_selectedOption == 2) {
            m_seekStepSec = std::clamp(m_seekStepSec + delta, 1, 60);
            m_configService.SetSeekStepSeconds(m_seekStepSec);
        }
    } else if (m_selectedCategory == 1) {
        // Воспроизведение
        if (m_selectedOption == 0) {
            m_savePositionMode = (m_savePositionMode + delta % 3 + 3) % 3;
            m_configService.SetSavePositionMode(m_savePositionMode);
        } else if (m_selectedOption == 1) {
            m_autoPlay = !m_autoPlay;
            m_configService.SetAutoPlay(m_autoPlay);
        } else if (m_selectedOption == 2) {
            m_crossfadeEnabled = !m_crossfadeEnabled;
            m_configService.SetCrossfadeEnabled(m_crossfadeEnabled);
        } else if (m_selectedOption == 3) {
            if (!m_crossfadeEnabled) return;
            m_crossfadeMs = std::clamp(m_crossfadeMs + delta * 500, 500, 10000);
            m_configService.SetCrossfadeDurationMs(m_crossfadeMs);
        } else if (m_selectedOption == 4) {
            m_repeatMode = (m_repeatMode + delta % 3 + 3) % 3;
            m_configService.SetRepeatMode(m_repeatMode);
        } else if (m_selectedOption == 5) {
            m_shuffle = !m_shuffle;
            m_configService.SetShuffle(m_shuffle);
        } else if (m_selectedOption == 6) {
            m_gapless = !m_gapless;
            m_configService.SetGaplessPlayback(m_gapless);
        }
    } else {
        // Categories 2, 3, 4 (ftxui.cfg)
        auto items = GetItemsForCategory(m_selectedCategory);
        if (m_selectedOption >= 0 && m_selectedOption < static_cast<int>(items.size())) {
            const auto& item = items[m_selectedOption];
            std::string curVal = m_themeConfig.GetRawValue(item.section, item.key);

            if (item.type == SettingType::CHOICE) {
                if (item.section == "visualizer" && item.key == "mode") {
                    int modeVal = 0;
                    try { modeVal = std::stoi(curVal); } catch (...) {}
                    modeVal = (modeVal + delta % 3 + 3) % 3;
                    m_themeConfig.SetRawValue(item.section, item.key, std::to_string(modeVal));
                } else if (item.section == "visualizer" && item.key == "bar_width") {
                    int wVal = 1;
                    try { wVal = std::stoi(curVal); } catch (...) {}
                    wVal = (wVal == 1) ? 2 : 1;
                    m_themeConfig.SetRawValue(item.section, item.key, std::to_string(wVal));
                } else if (item.section == "visualizer" && item.key == "bar_spacing") {
                    int spVal = 1;
                    try { spVal = std::stoi(curVal); } catch (...) {}
                    spVal = (spVal == 0) ? 1 : 0;
                    m_themeConfig.SetRawValue(item.section, item.key, std::to_string(spVal));
                } else if (item.section == "background" && item.key == "mode") {
                    std::string nextMode = (curVal == "gradient") ? "mono" : "gradient";
                    m_themeConfig.SetRawValue(item.section, item.key, nextMode);
                }
            } else if (item.type == SettingType::TOGGLE) {
                bool isTrue = (curVal == "true" || curVal == "1" || curVal == "yes");
                m_themeConfig.SetRawValue(item.section, item.key, isTrue ? "false" : "true");
            } else if (item.type == SettingType::STEPPER) {
                float val = 0.0f;
                try { val = std::stof(curVal); } catch (...) {}
                val = std::clamp(val + delta * item.step, item.minVal, item.maxVal);
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(2) << val;
                m_themeConfig.SetRawValue(item.section, item.key, ss.str());
            } else if (item.type == SettingType::COLOR_INPUT) {
                RGB rgb = TuiThemeConfig::ParseRgbStruct(curVal, {128, 128, 128});
                rgb.r = std::clamp(rgb.r + delta * 15, 0, 255);
                rgb.g = std::clamp(rgb.g + delta * 15, 0, 255);
                rgb.b = std::clamp(rgb.b + delta * 15, 0, 255);
                std::string newVal = std::to_string(rgb.r) + ", " + std::to_string(rgb.g) + ", " + std::to_string(rgb.b);
                m_themeConfig.SetRawValue(item.section, item.key, newVal);
            }
        }
    }

    if (OnSettingsChanged) {
        OnSettingsChanged();
    }
}

ftxui::Element SettingsModalComponent::RenderCategories() {
    auto theme = m_theme;
    static const std::vector<std::string> cats = {
        "Общие",
        "Воспроизведение",
        "Визуализатор",
        "Фон и тема",
        "Цвета сервисов"
    };

    m_categoryBoxes.clear();
    m_categoryBoxes.resize(cats.size());

    std::vector<ftxui::Element> elems;
    elems.push_back(ftxui::text(" РАЗДЕЛЫ [Tab]") | ftxui::bold | ftxui::color(theme.textMuted));
    elems.push_back(ftxui::text(""));

    for (size_t i = 0; i < cats.size(); ++i) {
        bool isSelected = (static_cast<int>(i) == m_selectedCategory);
        bool isHovered = (static_cast<int>(i) == m_hoveredCategory);

        std::string prefix = isSelected ? "▶ " : "  ";
        ftxui::Element catElem = ftxui::text(prefix + cats[i]);

        if (isSelected) {
            catElem = catElem | ftxui::bold | ftxui::color(theme.accent) | ftxui::bgcolor(theme.activeRow);
        } else if (isHovered) {
            catElem = catElem | ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight);
        } else {
            catElem = catElem | ftxui::color(theme.text);
        }

        catElem = catElem | ftxui::reflect(m_categoryBoxes[i]);
        elems.push_back(std::move(catElem));
        elems.push_back(ftxui::text(""));
    }

    elems.push_back(ftxui::filler());

    return ftxui::vbox(std::move(elems))
        | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 20);
}

ftxui::Element SettingsModalComponent::RenderOptionsList(int bodyHeight) {
    m_lastBodyHeight = bodyHeight;
    auto theme = m_theme;
    auto items = GetItemsForCategory(m_selectedCategory);
    int totalOpts = static_cast<int>(items.size());

    // Dynamically calculate visible items to fill the entire body height!
    // Each item has 2 lines (title + value) + 1 blank line spacing
    int visibleItems = std::max(4, (bodyHeight + 1) / 3);
    int maxScroll = std::max(0, totalOpts - visibleItems);

    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);

    m_optionBoxes.resize(totalOpts);
    m_optMinusBoxes.resize(totalOpts);
    m_optPlusBoxes.resize(totalOpts);

    std::vector<ftxui::Element> rows;
    int endIdx = std::min(totalOpts, m_scrollOffset + visibleItems);

    for (int i = m_scrollOffset; i < endIdx; ++i) {
        const auto& item = items[i];
        bool isSel = (i == m_selectedOption);
        bool isEditingThis = (m_isEditing && m_editingCategory == m_selectedCategory && m_editingOption == i);

        // Disabled option logic: Crossfade duration when crossfade is disabled
        bool isDisabled = (m_selectedCategory == 1 && i == 3 && !m_crossfadeEnabled);

        std::string rawVal;
        if (m_selectedCategory == 0) {
            if (i == 0) rawVal = m_availableSources[m_sourceIndex];
            else if (i == 1) rawVal = std::to_string(m_cacheSizeMb) + " МБ";
            else if (i == 2) rawVal = std::to_string(m_seekStepSec) + " сек";
        } else if (m_selectedCategory == 1) {
            if (i == 0) {
                static const std::string saveNames[] = {"Не сохранять", "Только текущий трек", "Позиция и трек"};
                rawVal = saveNames[std::clamp(m_savePositionMode, 0, 2)];
            } else if (i == 1) rawVal = m_autoPlay ? "[X] Включено" : "[ ] Выключено";
            else if (i == 2) rawVal = m_crossfadeEnabled ? "[X] Включен" : "[ ] Выключен";
            else if (i == 3) rawVal = std::to_string(m_crossfadeMs) + " мс";
            else if (i == 4) {
                static const std::string repNames[] = {"Без повтора", "Повторять все", "Повторять один"};
                rawVal = repNames[std::clamp(m_repeatMode, 0, 2)];
            } else if (i == 5) rawVal = m_shuffle ? "[X] Включен" : "[ ] Выключен";
            else if (i == 6) rawVal = m_gapless ? "[X] Включен" : "[ ] Выключен";
        } else {
            rawVal = m_themeConfig.GetRawValue(item.section, item.key);
        }

        ftxui::Element rightControl;

        if (isDisabled) {
            rightControl = ftxui::text("< недоступно >") | ftxui::color(theme.textMuted);
        } else if (isEditingThis) {
            // Active inline text editing mode
            ftxui::Element swatchElem = ftxui::text("");
            if (item.type == SettingType::COLOR_INPUT) {
                ftxui::Color liveCol = TuiThemeConfig::ParseColor(m_editingBuffer, ftxui::Color::White);
                swatchElem = ftxui::hbox({
                    ftxui::text("[") | ftxui::color(theme.border),
                    ftxui::text("  ") | ftxui::bgcolor(liveCol),
                    ftxui::text("] ") | ftxui::color(theme.border)
                });
            } else if (item.type == SettingType::GRADIENT_INPUT) {
                auto stops = TuiThemeConfig::ParseColorList(m_editingBuffer, {});
                std::vector<ftxui::Element> stopBoxes;
                stopBoxes.push_back(ftxui::text("[") | ftxui::color(theme.border));
                for (const auto& s : stops) {
                    stopBoxes.push_back(ftxui::text(" ") | ftxui::bgcolor(ftxui::Color::RGB(s.r, s.g, s.b)));
                }
                stopBoxes.push_back(ftxui::text("] ") | ftxui::color(theme.border));
                swatchElem = ftxui::hbox(std::move(stopBoxes));
            }

            std::string before = m_editingBuffer.substr(0, std::clamp(m_editCursor, 0, static_cast<int>(m_editingBuffer.size())));
            ftxui::Element textWithCursor;
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                int next = m_editCursor + 1;
                while (next < static_cast<int>(m_editingBuffer.size()) && (static_cast<unsigned char>(m_editingBuffer[next]) & 0xC0) == 0x80) {
                    next++;
                }
                std::string curChar = m_editingBuffer.substr(m_editCursor, next - m_editCursor);
                std::string after = m_editingBuffer.substr(next);

                textWithCursor = ftxui::hbox({
                    ftxui::text(before) | ftxui::bold | ftxui::color(ftxui::Color::White),
                    ftxui::text(curChar) | ftxui::bold | ftxui::color(ftxui::Color::Black) | ftxui::bgcolor(theme.accentCyan),
                    ftxui::text(after) | ftxui::bold | ftxui::color(ftxui::Color::White)
                });
            } else {
                textWithCursor = ftxui::hbox({
                    ftxui::text(before) | ftxui::bold | ftxui::color(ftxui::Color::White),
                    ftxui::text("█") | ftxui::bold | ftxui::color(theme.accentCyan)
                });
            }

            rightControl = ftxui::hbox({
                swatchElem,
                ftxui::text("[ ") | ftxui::bold | ftxui::color(theme.accentCyan),
                std::move(textWithCursor),
                ftxui::text(" ]") | ftxui::bold | ftxui::color(theme.accentCyan)
            });
        } else if (item.type == SettingType::CHOICE) {
            std::string displayVal = rawVal;
            if (item.section == "visualizer" && item.key == "mode") {
                int m = 0; try { m = std::stoi(rawVal); } catch (...) {}
                static const std::string visModes[] = {"CAVA Эквалайзер", "Симметричная волна", "Шелковая волна"};
                displayVal = visModes[std::clamp(m, 0, 2)];
            } else if (item.section == "visualizer" && item.key == "bar_width") {
                displayVal = (rawVal == "2") ? "2 знакоместа" : "1 знакоместо";
            } else if (item.section == "visualizer" && item.key == "bar_spacing") {
                displayVal = (rawVal == "0") ? "0: Вплотную" : "1: Через пробел";
            } else if (item.section == "background" && item.key == "mode") {
                displayVal = (rawVal == "gradient") ? "Вертикальный градиент" : "Сплошной цвет (mono)";
            }

            rightControl = ftxui::hbox({
                ftxui::text("[<] ") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optMinusBoxes[i]),
                ftxui::text("< " + displayVal + " >") | ftxui::bold | ftxui::color(theme.accentOrange),
                ftxui::text(" [>]") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optPlusBoxes[i])
            });
        } else if (item.type == SettingType::STEPPER) {
            std::string displayVal = "< " + rawVal + " >";
            rightControl = ftxui::hbox({
                ftxui::text("[-] ") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optMinusBoxes[i]),
                ftxui::text(displayVal) | ftxui::bold | ftxui::color(theme.accentOrange),
                ftxui::text(" [+]") | ftxui::bold | ftxui::color(theme.accentCyan) | ftxui::reflect(m_optPlusBoxes[i])
            });
        } else if (item.type == SettingType::TOGGLE) {
            bool isOn = (rawVal == "true" || rawVal == "1" || rawVal == "yes" || rawVal.find("[X]") != std::string::npos);
            std::string togStr = isOn ? "[X] Включено" : "[ ] Выключено";
            rightControl = ftxui::text(togStr) | ftxui::bold | ftxui::color(isOn ? theme.accent : theme.textMuted);
        } else if (item.type == SettingType::COLOR_INPUT) {
            ftxui::Color col = TuiThemeConfig::ParseColor(rawVal, ftxui::Color::White);
            rightControl = ftxui::hbox({
                ftxui::text("[") | ftxui::color(theme.border),
                ftxui::text("  ") | ftxui::bgcolor(col),
                ftxui::text("] ") | ftxui::color(theme.border),
                ftxui::text(rawVal) | ftxui::bold | ftxui::color(theme.accentOrange)
            });
        } else if (item.type == SettingType::GRADIENT_INPUT) {
            auto stops = TuiThemeConfig::ParseColorList(rawVal, {});
            std::vector<ftxui::Element> stopBoxes;
            stopBoxes.push_back(ftxui::text("[") | ftxui::color(theme.border));
            for (const auto& s : stops) {
                stopBoxes.push_back(ftxui::text(" ") | ftxui::bgcolor(ftxui::Color::RGB(s.r, s.g, s.b)));
            }
            stopBoxes.push_back(ftxui::text("] ") | ftxui::color(theme.border));

            rightControl = ftxui::hbox({
                ftxui::hbox(std::move(stopBoxes)),
                ftxui::text(" "),
                ftxui::text(rawVal) | ftxui::bold | ftxui::color(theme.accentOrange)
            });
        }

        std::string rowPrefix = isSel ? "▶ " : "  ";
        auto titleColor = isDisabled ? theme.textMuted : (isSel ? theme.accent : theme.text);

        // Line 1: Item title
        ftxui::Element titleRow = ftxui::hbox({
            ftxui::text(rowPrefix + item.label)
                | (isSel ? ftxui::bold : ftxui::nothing)
                | ftxui::color(titleColor),
            ftxui::filler()
        });

        // Line 2: Control / value with 6-space indentation (like HelpModal system info)
        ftxui::Element valRow = ftxui::hbox({
            ftxui::text("      "),
            rightControl,
            ftxui::filler()
        });

        ftxui::Element itemBox = ftxui::vbox({
            titleRow,
            valRow
        });

        if (isSel) {
            itemBox = itemBox | ftxui::bgcolor(theme.activeRow);
        }
        itemBox = itemBox | ftxui::reflect(m_optionBoxes[i]);

        rows.push_back(std::move(itemBox));
        if (i + 1 < endIdx) {
            rows.push_back(ftxui::text("")); // Blank line spacing between items
        }
    }

    auto listContent = ftxui::vbox(std::move(rows)) | ftxui::flex;

    // Interactive Right Scrollbar track: spans entire body height from top to bottom
    if (totalOpts > visibleItems) {
        std::vector<ftxui::Element> sbElems;
        bool canUp = (m_scrollOffset > 0);
        bool canDown = (m_scrollOffset < maxScroll);

        // Clickable Scroll Up button
        auto upBtn = ftxui::text(canUp ? "▲" : "─")
            | ftxui::color(canUp ? (m_isScrollUpHovered ? ftxui::Color::White : theme.accent) : theme.border)
            | (m_isScrollUpHovered && canUp ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollUpBox);
        sbElems.push_back(upBtn);

        // Track rows: clickable and draggable to span full bodyHeight - 2 rows
        int trackHeight = std::max(2, bodyHeight - 2);
        int thumbPos = (maxScroll > 0) ? (m_scrollOffset * (trackHeight - 1)) / maxScroll : 0;
        thumbPos = std::clamp(thumbPos, 0, trackHeight - 1);

        std::vector<ftxui::Element> trackRows;
        for (int t = 0; t < trackHeight; ++t) {
            if (t == thumbPos) {
                trackRows.push_back(ftxui::text("█") | ftxui::color(m_isScrollTrackHovered ? ftxui::Color::White : theme.accent));
            } else {
                trackRows.push_back(ftxui::text("│") | ftxui::color(theme.border));
            }
        }
        auto trackElem = ftxui::vbox(std::move(trackRows))
            | (m_isScrollTrackHovered ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollbarTrackBox);
        sbElems.push_back(trackElem);

        // Clickable Scroll Down button
        auto downBtn = ftxui::text(canDown ? "▼" : "─")
            | ftxui::color(canDown ? (m_isScrollDownHovered ? ftxui::Color::White : theme.accent) : theme.border)
            | (m_isScrollDownHovered && canDown ? ftxui::bgcolor(theme.highlight) : ftxui::nothing)
            | ftxui::reflect(m_scrollDownBox);
        sbElems.push_back(downBtn);

        auto scrollbar = ftxui::vbox(std::move(sbElems)) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);

        return ftxui::hbox({
            listContent,
            ftxui::text(" "),
            scrollbar
        }) | ftxui::flex;
    }

    return listContent;
}

ftxui::Element SettingsModalComponent::Render() {
    if (!m_isVisible) {
        return ftxui::text("");
    }

    auto theme = m_theme;
    auto items = GetItemsForCategory(m_selectedCategory);

    // Header with position counter
    std::string countStr = " [" + std::to_string(m_selectedOption + 1) + "/" + std::to_string(items.size()) + "] ";

    ftxui::Element closeBtn = ftxui::text(" [esc: закрыть] ")
        | (m_isCloseBtnHovered ? ftxui::color(ftxui::Color::White) | ftxui::bgcolor(theme.highlight)
                               : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_closeBtnBox);

    ftxui::Element topBar = ftxui::hbox({
        ftxui::text("  Настройки плеера ") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(countStr) | ftxui::color(theme.textMuted),
        ftxui::filler(),
        closeBtn,
        ftxui::text(" ")
    });

    int termWidth = ftxui::Terminal::Size().dimx;
    int termHeight = ftxui::Terminal::Size().dimy;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (winW > 20) termWidth = winW;
        if (winH > 10) termHeight = winH;
    }
#endif
    int modalWidth = std::clamp(static_cast<int>(termWidth * 0.78), 85, 115);
    int modalHeight = std::clamp(static_cast<int>(termHeight * 0.80), 26, 36);

    int overhead = m_showBottomBar ? 6 : 4;
    int bodyHeight = std::max(12, modalHeight - overhead);

    ftxui::Element body = ftxui::hbox({
        RenderCategories(),
        ftxui::separatorLight() | ftxui::color(theme.border),
        ftxui::text(" "),
        RenderOptionsList(bodyHeight) | ftxui::flex,
        ftxui::text(" ")
    }) | ftxui::flex;

    // Footer with context-dependent hints
    ftxui::Element footer;
    if (m_isEditing) {
        footer = ftxui::hbox({
            ftxui::filler(),
            ftxui::text(" [Ввод текста] RGB (R, G, B) или HEX (#RRGGBB)  [Enter] Сохранить  [Esc] Отмена ") | ftxui::bold | ftxui::color(theme.accentOrange),
            ftxui::filler()
        });
    } else {
        footer = ftxui::hbox({
            ftxui::filler(),
            ftxui::text(" [Tab] Разделы  [↑/↓] Пункт  [←/→] Значение  [Enter] Редактировать  [Esc] Закрыть ") | ftxui::color(theme.textMuted),
            ftxui::filler()
        });
    }

    std::vector<ftxui::Element> modalChildren;
    modalChildren.push_back(topBar);
    modalChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
    modalChildren.push_back(std::move(body) | ftxui::flex);
    if (m_showBottomBar) {
        modalChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
        modalChildren.push_back(footer);
    }

    auto modalWindow = ftxui::vbox(std::move(modalChildren))
    | ftxui::borderRounded
    | ftxui::color(theme.border)
    | ftxui::bgcolor(theme.panelBg)
    | ftxui::clear_under
    | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, modalWidth)
    | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, modalHeight)
    | ftxui::reflect(m_modalBox);

    return ftxui::vbox({
        ftxui::filler(),
        ftxui::hbox({
            ftxui::filler(),
            modalWindow,
            ftxui::filler()
        }),
        ftxui::filler()
    });
}

bool SettingsModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) {
        return false;
    }

    auto items = GetItemsForCategory(m_selectedCategory);
    int maxOpts = static_cast<int>(items.size());
    int visibleItems = std::max(4, (m_lastBodyHeight + 1) / 3);
    int maxScroll = std::max(0, maxOpts - visibleItems);

    // 1. Text editing mode interception
    if (m_isEditing) {
        if (event == ftxui::Event::Escape) {
            m_isEditing = false;
            return true;
        }
        if (event == ftxui::Event::Return) {
            if (m_editingOption >= 0 && m_editingOption < maxOpts) {
                const auto& item = items[m_editingOption];
                m_themeConfig.SetRawValue(item.section, item.key, m_editingBuffer);
                if (OnSettingsChanged) OnSettingsChanged();
            }
            m_isEditing = false;
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            if (m_editCursor > 0) {
                int prev = m_editCursor - 1;
                while (prev > 0 && (static_cast<unsigned char>(m_editingBuffer[prev]) & 0xC0) == 0x80) {
                    prev--;
                }
                m_editCursor = prev;
            }
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                int next = m_editCursor + 1;
                while (next < static_cast<int>(m_editingBuffer.size()) && (static_cast<unsigned char>(m_editingBuffer[next]) & 0xC0) == 0x80) {
                    next++;
                }
                m_editCursor = next;
            }
            return true;
        }
        if (event == ftxui::Event::Home) {
            m_editCursor = 0;
            return true;
        }
        if (event == ftxui::Event::End) {
            m_editCursor = static_cast<int>(m_editingBuffer.size());
            return true;
        }
        if (event == ftxui::Event::Backspace) {
            if (m_editCursor > 0) {
                int prev = m_editCursor - 1;
                while (prev > 0 && (static_cast<unsigned char>(m_editingBuffer[prev]) & 0xC0) == 0x80) {
                    prev--;
                }
                int count = m_editCursor - prev;
                m_editingBuffer.erase(prev, count);
                m_editCursor = prev;
            }
            return true;
        }
        if (event == ftxui::Event::Delete) {
            if (m_editCursor < static_cast<int>(m_editingBuffer.size())) {
                int next = m_editCursor + 1;
                while (next < static_cast<int>(m_editingBuffer.size()) && (static_cast<unsigned char>(m_editingBuffer[next]) & 0xC0) == 0x80) {
                    next++;
                }
                int count = next - m_editCursor;
                m_editingBuffer.erase(m_editCursor, count);
            }
            return true;
        }
        if (event.is_character()) {
            std::string ch = event.character();
            m_editingBuffer.insert(m_editCursor, ch);
            m_editCursor += static_cast<int>(ch.size());
            return true;
        }
        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (m_editingOption >= 0 && m_editingOption < maxOpts) {
                    const auto& item = items[m_editingOption];
                    m_themeConfig.SetRawValue(item.section, item.key, m_editingBuffer);
                    if (OnSettingsChanged) OnSettingsChanged();
                }
                m_isEditing = false;
            } else {
                return true;
            }
        } else {
            return true;
        }
    }

    // Toggle bottom hints panel with Shift+I or i / ш / Ш
    if (event == ftxui::Event::Character('I') || event == ftxui::Event::Character('i') ||
        event.character() == "Ш" || event.character() == "ш") {
        if (OnToggleBottomBarRequested) {
            OnToggleBottomBarRequested();
        } else {
            m_showBottomBar = !m_showBottomBar;
        }
        return true;
    }

    // 2. Mouse Handling
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_isCloseBtnHovered = m_closeBtnBox.Contain(mouse.x, mouse.y);
        m_isScrollUpHovered = m_scrollUpBox.Contain(mouse.x, mouse.y);
        m_isScrollDownHovered = m_scrollDownBox.Contain(mouse.x, mouse.y);
        m_isScrollTrackHovered = m_scrollbarTrackBox.Contain(mouse.x, mouse.y);

        m_hoveredCategory = -1;
        for (size_t i = 0; i < m_categoryBoxes.size(); ++i) {
            if (m_categoryBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredCategory = static_cast<int>(i);
                break;
            }
        }

        m_hoveredOption = -1;
        for (size_t i = 0; i < m_optionBoxes.size(); ++i) {
            if (m_optionBoxes[i].Contain(mouse.x, mouse.y)) {
                m_hoveredOption = static_cast<int>(i);
                break;
            }
        }

        if (mouse.button == ftxui::Mouse::WheelUp) {
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
            }
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelDown) {
            if (m_scrollOffset < maxScroll) {
                m_scrollOffset++;
            }
            return true;
        }

        if (mouse.button == ftxui::Mouse::Left) {
            if (mouse.motion == ftxui::Mouse::Pressed) {
                // Close button or click outside modal
                if (m_isCloseBtnHovered || !m_modalBox.Contain(mouse.x, mouse.y)) {
                    Hide();
                    if (OnCloseRequested) OnCloseRequested();
                    return true;
                }

                // Interactive Scrollbar Up button (scroll viewport without moving selected item)
                if (m_isScrollUpHovered) {
                    if (m_scrollOffset > 0) {
                        m_scrollOffset--;
                    }
                    return true;
                }

                // Interactive Scrollbar Down button (scroll viewport without moving selected item)
                if (m_isScrollDownHovered) {
                    if (m_scrollOffset < maxScroll) {
                        m_scrollOffset++;
                    }
                    return true;
                }

                // Interactive Scrollbar Track Click / Drag (smoothly scrolls viewport)
                if (m_isScrollTrackHovered || m_isDraggingScrollbar) {
                    m_isDraggingScrollbar = true;
                    int trackH = m_scrollbarTrackBox.y_max - m_scrollbarTrackBox.y_min;
                    if (trackH > 0 && maxScroll > 0) {
                        float ratio = static_cast<float>(mouse.y - m_scrollbarTrackBox.y_min) / static_cast<float>(trackH);
                        ratio = std::clamp(ratio, 0.0f, 1.0f);
                        m_scrollOffset = std::clamp(static_cast<int>(std::round(ratio * maxScroll)), 0, maxScroll);
                    }
                    return true;
                }

                // Category Click
                if (m_hoveredCategory >= 0) {
                    m_selectedCategory = m_hoveredCategory;
                    m_selectedOption = 0;
                    m_scrollOffset = 0;
                    return true;
                }

                // Option Minus button Click
                for (size_t i = 0; i < m_optMinusBoxes.size(); ++i) {
                    if (m_optMinusBoxes[i].Contain(mouse.x, mouse.y)) {
                        m_selectedOption = static_cast<int>(i);
                        ChangeOptionValue(-1);
                        return true;
                    }
                }

                // Option Plus button Click
                for (size_t i = 0; i < m_optPlusBoxes.size(); ++i) {
                    if (m_optPlusBoxes[i].Contain(mouse.x, mouse.y)) {
                        m_selectedOption = static_cast<int>(i);
                        ChangeOptionValue(+1);
                        return true;
                    }
                }

                // Option Row Click
                if (m_hoveredOption >= 0) {
                    m_selectedOption = m_hoveredOption;
                    const auto& item = items[m_selectedOption];
                    if (item.type == SettingType::COLOR_INPUT || item.type == SettingType::GRADIENT_INPUT) {
                        m_isEditing = true;
                        m_editingCategory = m_selectedCategory;
                        m_editingOption = m_selectedOption;
                        m_editingBuffer = m_themeConfig.GetRawValue(item.section, item.key);
                        m_editCursor = static_cast<int>(m_editingBuffer.size());
                    } else {
                        ChangeOptionValue(+1);
                    }
                    return true;
                }
            } else if (mouse.motion == ftxui::Mouse::Released) {
                m_isDraggingScrollbar = false;
            }
        }
        return true;
    }

    // 3. Keyboard Handling
    if (event == ftxui::Event::Escape) {
        Hide();
        if (OnCloseRequested) OnCloseRequested();
        return true;
    }

    // Tab directly cycles categories (0..4)
    if (event == ftxui::Event::Tab) {
        m_selectedCategory = (m_selectedCategory + 1) % 5;
        m_selectedOption = 0;
        m_scrollOffset = 0;
        return true;
    }
    if (event == ftxui::Event::TabReverse) {
        m_selectedCategory = (m_selectedCategory + 4) % 5;
        m_selectedOption = 0;
        m_scrollOffset = 0;
        return true;
    }

    // Navigation inside options list
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k') || event == ftxui::Event::Character('K')) {
        if (m_selectedOption > 0) {
            m_selectedOption--;
            // Skip disabled crossfade duration
            if (m_selectedCategory == 1 && !m_crossfadeEnabled && m_selectedOption == 3) {
                m_selectedOption = (m_selectedOption > 0) ? m_selectedOption - 1 : 0;
            }
            if (m_selectedOption < m_scrollOffset) {
                m_scrollOffset = m_selectedOption;
            }
        }
        return true;
    }

    if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j') || event == ftxui::Event::Character('J')) {
        if (m_selectedOption < maxOpts - 1) {
            m_selectedOption++;
            // Skip disabled crossfade duration
            if (m_selectedCategory == 1 && !m_crossfadeEnabled && m_selectedOption == 3) {
                if (m_selectedOption < maxOpts - 1) {
                    m_selectedOption++;
                } else {
                    m_selectedOption = 2;
                }
            }
            if (m_selectedOption >= m_scrollOffset + visibleItems) {
                m_scrollOffset = m_selectedOption - visibleItems + 1;
            }
        }
        return true;
    }

    if (event == ftxui::Event::ArrowLeft) {
        ChangeOptionValue(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowRight) {
        ChangeOptionValue(+1);
        return true;
    }

    if (event == ftxui::Event::Return || event == ftxui::Event::Character(' ')) {
        if (m_selectedOption >= 0 && m_selectedOption < maxOpts) {
            const auto& item = items[m_selectedOption];
            if (item.type == SettingType::COLOR_INPUT || item.type == SettingType::GRADIENT_INPUT) {
                m_isEditing = true;
                m_editingCategory = m_selectedCategory;
                m_editingOption = m_selectedOption;
                m_editingBuffer = m_themeConfig.GetRawValue(item.section, item.key);
                m_editCursor = static_cast<int>(m_editingBuffer.size());
                return true;
            }
        }
        ChangeOptionValue(+1);
        return true;
    }

    return true;
}

} // namespace tui
