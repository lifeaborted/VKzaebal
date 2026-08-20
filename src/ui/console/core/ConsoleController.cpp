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
        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text;
            std::cout.flush();
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

    m_dispatcher->OnSourceChangeRequested = [this](const std::string&) {
        m_currentState = ConsoleState::SELECT_SOURCE;
    };

    m_dispatcher->OnLogoutRequested = [this](const std::string& service) {
        auto processLogout = [this](const QString& svcName, const std::string& internalName) {
            m_authManager.ClearSavedToken(svcName);

            std::string msg = "[Выход] Токен для " + internalName + " успешно удален.\n\n> ";
            QMetaObject::invokeMethod(QCoreApplication::instance(), [msg]() {
                std::lock_guard<std::mutex> lock(Logger::GetMutex());
                std::cout << "\r\033[2K\033[1A\r\033[2K" << msg;
                std::cout.flush();
            }, Qt::QueuedConnection);
        };

        bool logoutVk = (service == "vk" || service == "all");
        bool logoutSpotify = (service == "spotify" || service == "all");
        bool logoutSc = (service == "sc" || service == "all");

        if (logoutVk) processLogout("VK", "ВКонтакте");
        if (logoutSpotify) processLogout("Spotify", "Spotify");
        if (logoutSc) processLogout("SoundCloud", "SoundCloud");

        QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
        QString currentSource = settings.value("General/source", "").toString();

        bool activeLoggedOut = false;
        if (currentSource == "VK" && logoutVk) activeLoggedOut = true;
        if (currentSource == "Spotify" && logoutSpotify) activeLoggedOut = true;
        if (currentSource == "SoundCloud" && logoutSc) activeLoggedOut = true;

        if (activeLoggedOut && m_currentProvider) {
            m_audio.Pause();
            m_playlist.Clear();
            emit SourceChanged("Offline");
        }
    };

    m_dispatcher->OnGaplessModeChanged = [this](bool isGapless) {
        if (OnGaplessModeChanged) OnGaplessModeChanged(isGapless);
    };

    m_dispatcher->OnQuitRequested = [this]() {
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

    // Прячем курсор (ANSI escape code)
    std::cout << "\033[?25l";
    std::cout.flush();

    m_isRunning = true;
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
    m_uiThread = std::thread(&ConsoleController::UiLoop, this);
}

void ConsoleController::Stop() {
    m_isRunning = false;

    std::cout << "\033[?25h";
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

    auto syncPrint = [this](const std::string& text) {
        if (!m_isRunning || !QCoreApplication::instance()) return;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text;
            std::cout.flush();
        }, Qt::QueuedConnection);
    };

    while (m_isRunning) {
        if (!std::getline(std::cin, rawInput)) {
            std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // === РЕЖИМ ОЖИДАНИЯ ТОКЕНА ===
        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> ";
                std::cout.flush();
                continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "offline") {
                m_currentState = ConsoleState::COMMAND_MODE;
                emit OfflineModeRequested();
                continue;
            }

            QString urlStr = QString::fromStdString(input);
            std::cout << "Обработка ссылки...\n\n> ";
            std::cout.flush();

            QMetaObject::invokeMethod(&m_authManager, [&, urlStr]() {
                m_authManager.onUrlIntercepted(urlStr);
            }, Qt::QueuedConnection);

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        // === РЕЖИМ ВЫБОРА ИСТОЧНИКА ===
        if (m_currentState == ConsoleState::SELECT_SOURCE) {
            size_t start = rawInput.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                std::cout << "> "; std::cout.flush(); continue;
            }
            std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

            if (input == "1") {
                emit SourceChanged("VK");
            } else if (input == "2") {
                emit SourceChanged("Spotify");
            } else if (input == "3") {
                emit SourceChanged("SoundCloud");
            } else if (input == "4") {
                emit SourceChanged("Offline");
            } else {
                syncPrint("[Ошибка] Неверный выбор. Введите 1-4:\n> ");
                continue;
            }
            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        // === РЕЖИМ ПЛЕЕРА (COMMAND_MODE) ===
        std::cout << "\033[1A\r\033[2K";
        std::cout.flush();

        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            std::cout << "> ";
            std::cout.flush();
            continue;
        }
        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

        if (m_currentState == ConsoleState::COMMAND_MODE) {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(m_renderer->IsVisualizerEnabled() ? 50 : 500));
    }
}

void ConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
    if (m_dispatcher) m_dispatcher->SetCurrentProvider(provider);
}