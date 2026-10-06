#pragma once

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>

class IAudioEngine;
class PlaylistManager;
class ConfigurationService;

namespace tui {

class TuiModalManager;
class TuiCommandBar;

// ==============================================================================
// TuiInputRouter
// ------------------------------------------------------------------------------
// Маршрутизатор горячих клавиш и пользовательского ввода.
// Реализует паттерн Chain of Responsibility (Цепочка обязанностей):
// 1. Активное модальное окно
// 2. Командная строка '/'
// 3. Поле ввода поиска (блокировка медиа-клавиш при наборе текста)
// 4. Глобальные мультимедиа клавиши (Громкость, Перемотка, Пауза/Плей)
// 5. Навигация и функциональные клавиши (Табы, Справка, Выход)
// ==============================================================================
class TuiInputRouter {
public:
    struct Callbacks {
        std::function<int()> getActiveScreenIndex;
        std::function<bool()> isTypingInSearch;
        std::function<void(int screenIndex)> switchScreen;
        std::function<void()> toggleSearchMode;
        std::function<void()> toggleBottomBar;
        std::function<void()> toggleSidebar;
        std::function<void()> openPlaylistModal;
        std::function<void(bool showSystemInfo)> openHelpModal;
        std::function<void()> openSettingsModal;
        std::function<void()> cycleVisualizerMode;
        std::function<void()> toggleLike;
        std::function<void()> quitRequested;
    };

    TuiInputRouter(
        TuiModalManager& modalManager,
        TuiCommandBar& commandBar,
        IAudioEngine& audio,
        PlaylistManager& playlist,
        ConfigurationService* configService,
        ftxui::ScreenInteractive& screen,
        Callbacks callbacks
    );

    bool RouteEvent(ftxui::Event event);

private:
    TuiModalManager& m_modalManager;
    TuiCommandBar& m_commandBar;
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    ConfigurationService* m_configService = nullptr;
    ftxui::ScreenInteractive& m_screen;
    Callbacks m_callbacks;
};

} // namespace tui
