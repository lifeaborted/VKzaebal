#ifdef _WIN32
#include <windows.h>
#endif

#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QRegularExpression>
#include <iostream>
#include <string>
#include <cctype>
#include <QCoreApplication>
#include <QMetaObject>
#include <cmath>
#include <QSettings>

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

ConsoleController::ConsoleController(
    IAudioEngine& audio, PlaylistManager& playlist, OAuthManager& authManager,
    DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher
) : m_audio(audio), m_playlist(playlist), m_authManager(authManager),
    m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher),
    m_currentState(ConsoleState::COMMAND_MODE), m_isRunning(false) {

    m_dispatcher = std::make_unique<CommandDispatcher>(audio, playlist, dbManager, downloader, lyricsFetcher);
    m_renderer = std::make_unique<ConsoleRenderer>(audio, playlist);

    m_dispatcher->SetPrintCallback([this](const std::string& text) {
            if (!m_isRunning || !QCoreApplication::instance()) return;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, text]() {
                std::string cleanText = text;
                size_t pos = cleanText.find("\n\n> ");
                if (pos != std::string::npos) cleanText.erase(pos);

                // Если текст длинный (например, меню Help или Search) - кидаем в Оверлей
                int newlines = std::count(cleanText.begin(), cleanText.end(), '\n');
                if (newlines > 2) {
                    m_renderer->SetOverlay(cleanText);
                } else {
                    m_renderer->SetStatusMessage(cleanText);
                }
            }, Qt::QueuedConnection);
        });

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_renderer->SetVisualizerEnabled(settings.value("Ui/ShowVisualizer", true).toBool());

    m_dispatcher->OnVisualizerToggled = [this]() {
        bool newState = !m_renderer->IsVisualizerEnabled();
        m_renderer->SetVisualizerEnabled(newState);
        QSettings s(PathManager::GetConfigPath(), QSettings::IniFormat);
        s.setValue("Ui/ShowVisualizer", newState);
        s.sync();
    };

    m_dispatcher->OnReloadUiRequested = [this]() {
        m_renderer->ReloadConfig();
    };

    m_dispatcher->OnSourceChangeRequested = [this](const std::string&) {
        m_currentState = ConsoleState::SELECT_SOURCE;
    };

    m_dispatcher->OnLogoutRequested = [this](const std::string& service) {
        auto processLogout = [this](const QString& svcName, const std::string& internalName) {
            m_authManager.ClearSavedToken(svcName);
            std::string msg = "[Выход] Токен для " + internalName + " удален.";
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, msg]() {
                m_renderer->SetStatusMessage(msg);
            }, Qt::QueuedConnection);
        };

        bool logoutVk = (service == "vk" || service == "all");
        bool logoutSpotify = (service == "spotify" || service == "all");
        bool logoutSc = (service == "sc" || service == "all");
        bool logoutYandex = (service == "yandex" || service == "all");

        if (logoutVk) processLogout("VK", "ВКонтакте");
        if (logoutSpotify) processLogout("Spotify", "Spotify");
        if (logoutSc) processLogout("SoundCloud", "SoundCloud");
        if (logoutYandex) processLogout("Yandex", "Yandex");
    };

    m_dispatcher->OnGaplessModeChanged = [this](bool isGapless) {
        if (OnGaplessModeChanged) OnGaplessModeChanged(isGapless);
    };

    m_dispatcher->OnQuitRequested = [this]() {
        m_audio.Pause();
        emit QuitRequested();
        m_isRunning = false;
    };
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
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
    m_uiThread = std::thread(&ConsoleController::UiLoop, this);
}

void ConsoleController::Stop() {
    m_isRunning = false;

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    int mode = settings.value("Visualizer/Mode", 0).toInt();

    if (mode == 1) {
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

    if (m_inputThread.joinable()) m_inputThread.detach();
    if (m_uiThread.joinable()) m_uiThread.join();
}

void ConsoleController::InputLoop() {
    std::string rawInput;

    while (m_isRunning) {
        if (!std::getline(std::cin, rawInput)) {
            std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> "; std::cout.flush(); continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "offline") {
                m_currentState = ConsoleState::COMMAND_MODE;
                emit OfflineModeRequested();
                continue;
            }

            QString urlStr = QString::fromStdString(input);
            QMetaObject::invokeMethod(&m_authManager, [&, urlStr]() {
                m_authManager.onUrlIntercepted(urlStr);
            }, Qt::QueuedConnection);

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        if (m_currentState == ConsoleState::SELECT_SOURCE) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> "; std::cout.flush(); continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "1") emit SourceChanged("VK");
            else if (input == "2") emit SourceChanged("Spotify");
            else if (input == "3") emit SourceChanged("SoundCloud");
            else if (input == "4") emit SourceChanged("Yandex");
            else if (input == "5") emit SourceChanged("Offline");

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        // РЕЖИМ ПЛЕЕРА
        if (m_currentState == ConsoleState::COMMAND_MODE) {
            // При нажатии Enter всегда просим перерисовать экран, чтобы убрать сдвиг скролла
            m_renderer->RequestFullRedraw();
            m_renderer->SetOverlay(""); // ВСЕГДА прячем оверлей Help/Search

            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                continue; // Пустой Enter просто закрыл оверлей и обновил кадр
            }

            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);
            m_dispatcher->Dispatch(input);
            continue;
        }
    }
}

void ConsoleController::UiLoop() {
    while (m_isRunning) {
        if (m_currentState == ConsoleState::WAITING_TOKEN_URL || m_currentState == ConsoleState::SELECT_SOURCE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (m_currentState == ConsoleState::COMMAND_MODE) {
            m_renderer->Render();
        }

        // ДИНАМИЧЕСКИЙ FPS В ЗАВИСИМОСТИ ОТ РЕЖИМА
        QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
        int mode = settings.value("Visualizer/Mode", 0).toInt();

        int fps = 2;
        if (mode == 1) {
            fps = m_renderer->GetFramerate();
        } else {
            fps = m_renderer->IsVisualizerEnabled() ? 15 : 2;
        }

        int delay = (fps > 0) ? (1000 / fps) : 33;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

void ConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
    if (m_dispatcher) m_dispatcher->SetCurrentProvider(provider);
}