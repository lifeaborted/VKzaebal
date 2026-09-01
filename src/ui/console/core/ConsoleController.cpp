#ifdef _WIN32
#include <windows.h>
#endif

#include "ConsoleController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/oauth/OAuthManager.h"
#include "services/database/DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "core/api/IAudioProvider.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "ui/console/commands/CommandDispatcher.h"
#include "ui/console/view/ConsoleRenderer.h"

#include <QCoreApplication>
#include <QTimer>
#include <QSettings>
#include <iostream>

ConsoleController::ConsoleController(
    IAudioEngine& audio, PlaylistManager& playlist, OAuthManager& authManager,
    DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher, QObject* parent
) : QObject(parent), m_audio(audio), m_playlist(playlist), m_authManager(authManager),
    m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher),
    m_currentState(ConsoleState::COMMAND_MODE), m_isRunning(false) {

    m_dispatcher = std::make_unique<CommandDispatcher>(audio, playlist, dbManager, downloader, lyricsFetcher);
    m_renderer = std::make_unique<ConsoleRenderer>(audio, playlist);

    m_dispatcher->SetPrintCallback([this](const std::string& text) {
        if (!m_isRunning || !QCoreApplication::instance()) return;

        std::string cleanText = text;
        size_t pos = cleanText.find("\n\n> ");
        if (pos != std::string::npos) cleanText.erase(pos);

        int newlines = std::count(cleanText.begin(), cleanText.end(), '\n');
        if (newlines > 2) {
            m_renderer->SetOverlay(cleanText);
        } else {
            m_renderer->SetStatusMessage(cleanText);
        }
    });

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_renderer->SetVisualizerEnabled(settings.value("Ui/ShowVisualizer", true).toBool());

    m_dispatcher->OnVisualizerToggled = [this]() {
        bool newState = !m_renderer->IsVisualizerEnabled();
        m_renderer->SetVisualizerEnabled(newState);
        QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Ui/ShowVisualizer", newState);
    };

    m_dispatcher->OnReloadUiRequested = [this]() { m_renderer->ReloadConfig(); };
    m_dispatcher->OnSourceChangeRequested = [this](const std::string&) { m_currentState = ConsoleState::SELECT_SOURCE; };

    m_dispatcher->OnLogoutRequested = [this](const std::string& service) {
        auto processLogout = [this](const QString& svcName, const std::string& internalName) {
            m_authManager.ClearSavedToken(svcName);
            m_renderer->SetStatusMessage("[Выход] Токен для " + internalName + " удален.");
        };

        if (service == "vk" || service == "all") processLogout("VK", "ВКонтакте");
        if (service == "spotify" || service == "all") processLogout("Spotify", "Spotify");
        if (service == "sc" || service == "all") processLogout("SoundCloud", "SoundCloud");
        if (service == "yandex" || service == "all") processLogout("Yandex", "Yandex");
    };

    m_dispatcher->OnGaplessModeChanged = [this](bool isGapless) {
        if (OnGaplessModeChanged) OnGaplessModeChanged(isGapless);
    };

    m_dispatcher->OnQuitRequested = [this]() {
        m_audio.Pause();
        emit QuitRequested();
    };

    // Таймер для отрисовки интерфейса (Главный поток)
    m_uiTimer = new QTimer(this);
    m_uiTimer->setTimerType(Qt::PreciseTimer);
    connect(m_uiTimer, &QTimer::timeout, this, &ConsoleController::OnUiTick);
}

ConsoleController::~ConsoleController() {
    Stop();
}

void ConsoleController::SetState(ConsoleState state) {
    m_currentState = state;
}

void ConsoleController::Start() {
    if (m_isRunning) return;

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    int mode = settings.value("Visualizer/Mode", 0).toInt();

    if (mode == 1) {
        std::cout << "\033[?1049h\033[2J\033[999;1H> ";
        std::cout.flush();
    }

    m_isRunning = true;
    m_uiTimer->start(16); // Запуск визуализатора

    // Запускаем фоновый поток только для чтения клавиатуры!
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
}

void ConsoleController::Stop() {
    if (!m_isRunning) return;
    m_isRunning = false;

    m_uiTimer->stop();

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    if (settings.value("Visualizer/Mode", 0).toInt() == 1) {
        std::cout << "\033[?1049l\033[?25h";
    } else {
        std::cout << "\033[?25h";
    }
    std::cout.flush();

#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin != INVALID_HANDLE_VALUE) {
        CancelIoEx(hStdin, NULL);
    }
#endif

    // Отсоединяем поток, чтобы он мирно умер при закрытии приложения, если застрял на std::getline
    if (m_inputThread.joinable()) {
        m_inputThread.detach();
    }
}

void ConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
    if (m_dispatcher) m_dispatcher->SetCurrentProvider(provider);
}

void ConsoleController::InputLoop() {
    std::string rawInput;
    while (m_isRunning) {
        // БЛОКИРУЮЩИЙ ВЫЗОВ (Теперь он не мешает графике, так как живет в своем потоке)
        if (!std::getline(std::cin, rawInput)) {
            std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // БЕЗОПАСНО ПЕРЕДАЕМ КОМАНДУ В ГЛАВНЫЙ ПОТОК (Qt Event Loop)
        QMetaObject::invokeMethod(this, [this, rawInput]() {
            if (!m_isRunning) return;

            if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
                size_t start = rawInput.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) { std::cout << "> "; std::cout.flush(); return; }

                std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);
                if (input == "offline") {
                    m_currentState = ConsoleState::COMMAND_MODE;
                    emit OfflineModeRequested();
                    return;
                }

                m_authManager.onUrlIntercepted(QString::fromStdString(input));
                m_currentState = ConsoleState::COMMAND_MODE;
                return;
            }

            if (m_currentState == ConsoleState::SELECT_SOURCE) {
                        size_t start = rawInput.find_first_not_of(" \t\r\n");
                        if (start == std::string::npos) { std::cout << "> "; std::cout.flush(); return; }

                        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

                        m_renderer->SetOverlay("");
                        m_renderer->RequestFullRedraw();

                        if (input == "1") emit SourceChanged("VK");
                        else if (input == "2") emit SourceChanged("Spotify");
                        else if (input == "3") emit SourceChanged("SoundCloud");
                        else if (input == "4") emit SourceChanged("Yandex");
                        else if (input == "5") emit SourceChanged("Offline");

                        m_currentState = ConsoleState::COMMAND_MODE;
                        return;
                    }

            if (m_currentState == ConsoleState::COMMAND_MODE) {
                m_renderer->RequestFullRedraw();
                m_renderer->SetOverlay("");
                m_renderer->SetStatusMessage("");

                size_t start = rawInput.find_first_not_of(" \t\r\n");
                if (start != std::string::npos) {
                    std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);
                    m_dispatcher->Dispatch(input);
                }
            }
        }, Qt::QueuedConnection);
    }
}

void ConsoleController::OnUiTick() {
    if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
        return;
    }

        m_renderer->Render();

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    int mode = settings.value("Visualizer/Mode", 0).toInt();

    int fps = 2;
    if (mode == 1) fps = m_renderer->GetFramerate();
    else fps = m_renderer->IsVisualizerEnabled() ? 15 : 2;

    if (fps < 1) fps = 1;

    int newInterval = (fps >= 1000) ? 0 : (1000 / fps);

    if (m_uiTimer->interval() != newInterval) {
        m_uiTimer->setInterval(newInterval);
    }
}