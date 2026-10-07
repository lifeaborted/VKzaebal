#include "ui/tui/core/TuiInputRouter.h"
#include "ui/tui/core/TuiModalManager.h"
#include "ui/tui/core/TuiCommandBar.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/config/ConfigurationService.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <algorithm>

namespace tui {

TuiInputRouter::TuiInputRouter(
    TuiModalManager& modalManager,
    TuiCommandBar& commandBar,
    IAudioEngine& audio,
    PlaylistManager& playlist,
    ConfigurationService* configService,
    ftxui::ScreenInteractive& screen,
    Callbacks callbacks
) : m_modalManager(modalManager),
    m_commandBar(commandBar),
    m_audio(audio),
    m_playlist(playlist),
    m_configService(configService),
    m_screen(screen),
    m_callbacks(std::move(callbacks)) {}

bool TuiInputRouter::RouteEvent(ftxui::Event event) {
    // 0. События внутренней перерисовки FTXUI не должны перехватываться роутером ввода
    if (event == ftxui::Event::Custom) {
        return false;
    }

    // Нормализация Escape: при одновременном движении мыши в терминале
    // символ '\x1B' от клавиши Esc склеивается со следующим '\x1B' от mouse sequence (\x1B[<...),
    // образуя последовательность, начинающуюся с "\x1B\x1B".
    if (event == ftxui::Event::Escape ||
        (event.input().size() >= 2 && event.input()[0] == '\x1B' && event.input()[1] == '\x1B') ||
        event.input() == "\x1B") {
        event = ftxui::Event::Escape;
    }

    // Проверка глобального хоткея переключения нижней строки подсказок (Shift+I, i, ш, Ш)
    bool isBottomBarToggle = (event == ftxui::Event::Character('I') || event == ftxui::Event::Character('i') ||
                              event.character() == "Ш" || event.character() == "ш");

    // 1. Модальное окно перехватывает весь ввод, если активно
    if (m_modalManager.HasActiveModal()) {
        auto active = m_modalManager.GetActiveModal();
        if (isBottomBarToggle && active && !active->IsTyping()) {
            if (m_callbacks.toggleBottomBar) {
                m_callbacks.toggleBottomBar();
            }
            m_screen.PostEvent(ftxui::Event::Custom);
            return true;
        }

        bool handled = m_modalManager.HandleEvent(event);
        m_screen.PostEvent(ftxui::Event::Custom);
        return true;
    }

    // 2. Командная строка '/'
    if (m_commandBar.IsActive()) {
        bool handled = m_commandBar.OnEvent(event);
        m_screen.PostEvent(ftxui::Event::Custom);
        return handled;
    }

    bool isTyping = m_callbacks.isTypingInSearch ? m_callbacks.isTypingInSearch() : false;
    int activeScreen = m_callbacks.getActiveScreenIndex ? m_callbacks.getActiveScreenIndex() : 0;

    // 3. Глобальные клавиши мультимедиа (когда не идет набор текста в поиске)
    if (!isTyping) {
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('+') || event == ftxui::Event::Character('=')) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                m_audio.SetVolume(std::clamp(m_audio.GetVolume() + 0.05f, 0.0f, 1.0f));
            }, Qt::QueuedConnection);
            return true;
        }
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('-') || event == ftxui::Event::Character('_')) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                m_audio.SetVolume(std::clamp(m_audio.GetVolume() - 0.05f, 0.0f, 1.0f));
            }, Qt::QueuedConnection);
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                double step = m_configService ? static_cast<double>(m_configService->GetSeekStepSeconds()) : 5.0;
                double cur = m_audio.GetPositionSeconds();
                m_audio.SetPositionSeconds((std::max)(0.0, cur - step));
            }, Qt::QueuedConnection);
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                double step = m_configService ? static_cast<double>(m_configService->GetSeekStepSeconds()) : 5.0;
                double cur = m_audio.GetPositionSeconds();
                double tot = m_audio.GetLengthSeconds();
                if (tot <= 0.0) tot = static_cast<double>(m_playlist.GetCurrentTrack().duration);
                double target = cur + step;
                if (tot > 0.0 && target > tot) target = tot;
                m_audio.SetPositionSeconds(target);
            }, Qt::QueuedConnection);
            return true;
        }
        if (event == ftxui::Event::Character(' ')) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                if (m_audio.IsPlaying()) m_audio.Pause();
                else m_audio.Resume();
            }, Qt::QueuedConnection);
            return true;
        }
    }

    // 4. Tab переключение режима поиска
    if (event == ftxui::Event::Tab) {
        if (activeScreen == 0) {
            return true; // На главном экране Tab не выполняет действий
        } else if (activeScreen == 1) {
            if (m_callbacks.toggleSearchMode) {
                m_callbacks.toggleSearchMode();
                m_screen.PostEvent(ftxui::Event::Custom);
            }
            return true;
        }
    }

    // 5. Вызов командной строки '/'
    if (event == ftxui::Event::Character('/')) {
        if (isTyping) {
            return false;
        }
        m_commandBar.Open();
        m_screen.PostEvent(ftxui::Event::Custom);
        return true;
    }

    // 6. Подсказки горячих клавиш: Shift+I, i, ш, Ш
    if (!isTyping && isBottomBarToggle) {
        if (m_callbacks.toggleBottomBar) {
            m_callbacks.toggleBottomBar();
        }
        return true;
    }

    // 6b. Сворачивание / разворачивание боковой панели: [ или ] или х / Х / ъ / Ъ
    if (!isTyping && (event == ftxui::Event::Character('[') || event == ftxui::Event::Character(']') ||
                      event.character() == "х" || event.character() == "Х" ||
                      event.character() == "ъ" || event.character() == "Ъ")) {
        if (m_callbacks.toggleSidebar) {
            m_callbacks.toggleSidebar();
        }
        return true;
    }

    // 7. P -> Модалка менеджера плейлистов
    if (!isTyping && (event == ftxui::Event::Character('p') || event == ftxui::Event::Character('P') ||
                      event.character() == "З" || event.character() == "з")) {
        if (m_callbacks.openPlaylistModal) {
            m_callbacks.openPlaylistModal();
        }
        return true;
    }

    // 8. ? или F1 / H / h / р / Р -> Справка
    if (!isTyping && (event == ftxui::Event::Character('?') || event == ftxui::Event::F1 ||
                      event == ftxui::Event::Character('h') || event == ftxui::Event::Character('H') ||
                      event.character() == "р" || event.character() == "Р")) {
        if (m_callbacks.openHelpModal) {
            m_callbacks.openHelpModal(false);
        }
        return true;
    }

    // 9. O или F2 -> Настройки
    if (!isTyping && (event == ftxui::Event::F2 || event == ftxui::Event::Character('o') || event == ftxui::Event::Character('O') ||
                      event.character() == "Щ" || event.character() == "щ")) {
        if (m_callbacks.openSettingsModal) {
            m_callbacks.openSettingsModal();
        }
        return true;
    }

    // 9b. E или У -> Эквалайзер
    if (!isTyping && (event == ftxui::Event::Character('e') || event == ftxui::Event::Character('E') ||
                      event.character() == "у" || event.character() == "У")) {
        if (m_callbacks.openEqualizerModal) {
            m_callbacks.openEqualizerModal();
        }
        return true;
    }

    // 9. Навигация по экранам: F -> Поиск, Esc -> Now Playing
    if (event == ftxui::Event::Character('f') || event == ftxui::Event::Character('F') ||
        event.character() == "а" || event.character() == "А") {
        if (activeScreen == 0 && m_callbacks.switchScreen) {
            m_callbacks.switchScreen(1);
            return true;
        }
    }

    if (event == ftxui::Event::Escape) {
        if (activeScreen != 0 && m_callbacks.switchScreen) {
            m_callbacks.switchScreen(0);
            return true;
        }
    }

    // 10. Хоткеи экрана Now Playing (экран 0)
    if (activeScreen == 0) {
        // N -> Следующий трек
        if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N') ||
            event.character() == "т" || event.character() == "Т") {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                m_playlist.Next();
            }, Qt::QueuedConnection);
            return true;
        }

        // B -> Предыдущий трек
        if (event == ftxui::Event::Character('b') || event == ftxui::Event::Character('B') ||
            event.character() == "и" || event.character() == "И") {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                m_playlist.Previous();
            }, Qt::QueuedConnection);
            return true;
        }

        // L -> Лайк / Избранное
        if (event == ftxui::Event::Character('l') || event == ftxui::Event::Character('L') ||
            event.character() == "д" || event.character() == "Д") {
            if (m_callbacks.toggleLike) {
                m_callbacks.toggleLike();
            }
            return true;
        }

        // V -> Переключение стиля визуализатора
        if (event == ftxui::Event::Character('v') || event == ftxui::Event::Character('V') ||
            event.character() == "м" || event.character() == "М") {
            if (m_callbacks.cycleVisualizerMode) {
                m_callbacks.cycleVisualizerMode();
                m_screen.PostEvent(ftxui::Event::Custom);
            }
            return true;
        }
    }

    // 11. Q -> Выход
    if (!isTyping && (event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q') ||
                      event.character() == "й" || event.character() == "Й")) {
        m_screen.Exit();
        return true;
    }

    return false;
}

} // namespace tui
