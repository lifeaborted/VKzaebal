#include "PlaybackSettingsTab.h"
#include "services/config/ConfigurationService.h"
#include <algorithm>

namespace tui {

PlaybackSettingsTab::PlaybackSettingsTab(ConfigurationService& configService)
    : m_configService(configService) {
    LoadSettings();
}

void PlaybackSettingsTab::LoadSettings() {
    m_savePositionMode = m_configService.GetSavePositionMode();
    m_autoPlay = m_configService.GetAutoPlay();
    m_crossfadeEnabled = m_configService.GetCrossfadeEnabled();
    m_crossfadeMs = m_configService.GetCrossfadeDurationMs();
    if (m_crossfadeMs <= 0) m_crossfadeMs = 3000;
    m_repeatMode = m_configService.GetRepeatMode();
    m_shuffle = m_configService.GetShuffle();
    m_gapless = m_configService.GetGaplessPlayback();
    m_autoScroll = m_configService.GetAutoScroll();
    m_jumpToSourceTrack = m_configService.GetJumpToSourceTrack();
}

std::vector<SettingItem> PlaybackSettingsTab::GetItems() const {
    return {
        {"Сохранение позиции (save_pos)", "playback", "save_pos", SettingType::CHOICE, {"Не сохранять", "Только текущий трек", "Позиция и трек"}, 1, 0, 2},
        {"Автовоспроизведение (autoplay)", "session", "autoplay", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Плавный переход (crossfade)", "audio", "crossfade", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Длительность кроссфейда (мс)", "audio", "crossfade_ms", SettingType::STEPPER, {}, 500, 500, 10000},
        {"Режим повтора треков (repeat)", "session", "repeat", SettingType::CHOICE, {"Без повтора", "Повторять все", "Повторять один"}, 1, 0, 2},
        {"Случайный порядок (shuffle)", "session", "shuffle", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Непрерывный переход (gapless)", "audio", "gapless", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Автоскролл к треку (autoscroll)", "playback", "autoscroll", SettingType::TOGGLE, {}, 1, 0, 1},
        {"Переход к позиции трека (jump_pos)", "playback", "jump_pos", SettingType::TOGGLE, {}, 1, 0, 1}
    };
}

std::string PlaybackSettingsTab::GetCurrentValue(int optionIndex) const {
    switch (optionIndex) {
        case 0: {
            static const char* modes[] = {"Не сохранять", "Только текущий трек", "Позиция и трек"};
            if (m_savePositionMode >= 0 && m_savePositionMode < 3) return modes[m_savePositionMode];
            return modes[2];
        }
        case 1:
            return m_autoPlay ? "true" : "false";
        case 2:
            return m_crossfadeEnabled ? "true" : "false";
        case 3:
            return std::to_string(m_crossfadeMs) + " мс";
        case 4: {
            static const char* rmodes[] = {"Без повтора", "Повторять все", "Повторять один"};
            if (m_repeatMode >= 0 && m_repeatMode < 3) return rmodes[m_repeatMode];
            return rmodes[0];
        }
        case 5:
            return m_shuffle ? "true" : "false";
        case 6:
            return m_gapless ? "true" : "false";
        case 7:
            return m_autoScroll ? "true" : "false";
        case 8:
            return m_jumpToSourceTrack ? "true" : "false";
        default:
            return "";
    }
}

void PlaybackSettingsTab::ChangeValue(int optionIndex, int delta) {
    if (optionIndex == 0) {
        m_savePositionMode = (m_savePositionMode + delta % 3 + 3) % 3;
        m_configService.SetSavePositionMode(m_savePositionMode);
    } else if (optionIndex == 1) {
        m_autoPlay = !m_autoPlay;
        m_configService.SetAutoPlay(m_autoPlay);
    } else if (optionIndex == 2) {
        m_crossfadeEnabled = !m_crossfadeEnabled;
        m_configService.SetCrossfadeEnabled(m_crossfadeEnabled);
    } else if (optionIndex == 3) {
        if (!m_crossfadeEnabled) return;
        m_crossfadeMs = std::clamp(m_crossfadeMs + delta * 500, 500, 10000);
        m_configService.SetCrossfadeDurationMs(m_crossfadeMs);
    } else if (optionIndex == 4) {
        m_repeatMode = (m_repeatMode + delta % 3 + 3) % 3;
        m_configService.SetRepeatMode(m_repeatMode);
    } else if (optionIndex == 5) {
        m_shuffle = !m_shuffle;
        m_configService.SetShuffle(m_shuffle);
    } else if (optionIndex == 6) {
        m_gapless = !m_gapless;
        m_configService.SetGaplessPlayback(m_gapless);
    } else if (optionIndex == 7) {
        m_autoScroll = !m_autoScroll;
        m_configService.SetAutoScroll(m_autoScroll);
    } else if (optionIndex == 8) {
        m_jumpToSourceTrack = !m_jumpToSourceTrack;
        m_configService.SetJumpToSourceTrack(m_jumpToSourceTrack);
    }
}

void PlaybackSettingsTab::SetCurrentValue(int, const std::string&) {
}

bool PlaybackSettingsTab::IsOptionDisabled(int optionIndex) const {
    if (optionIndex == 3 && !m_crossfadeEnabled) {
        return true;
    }
    return false;
}

} // namespace tui
