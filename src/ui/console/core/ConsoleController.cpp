#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "ConsoleController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/oauth/OAuthManager.h"
#include "core/auth/oauth/WebViewCookieReader.h"
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
    DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher,
    QNetworkAccessManager* networkManager, QObject* parent
) : QObject(parent), m_audio(audio), m_playlist(playlist), m_authManager(authManager),
    m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher),
    m_currentState(ConsoleState::COMMAND_MODE), m_isRunning(false) {

    m_dispatcher = std::make_unique<CommandDispatcher>(audio, playlist, dbManager, downloader, lyricsFetcher, nullptr, nullptr, networkManager);
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
    m_dispatcher->OnSourceChangeRequested = [this](const std::string& target) {
        if (target == "SELECT" || target.empty()) {
            std::string menu = "=== Выбор источника ===\n\n"
                               "  [1] ВКонтакте\n"
                               "  [2] Spotify\n"
                               "  [3] SoundCloud\n"
                               "  [4] Yandex\n"
                               "  [5] YouTube\n"
                               "  [6] Оффлайн режим\n"
                               "  [7] Общий микс (Все сервисы)\n"
                               "  [8] Плейлисты\n\n"
                               "  [0] Отмена\n\n"
                               "Выберите номер: ";
            m_renderer->SetOverlay(menu);
            m_currentState = ConsoleState::SELECT_SOURCE;
        } else {
            emit SourceChanged(target);
        }
    };

    m_dispatcher->OnLogoutRequested = [this](const std::string& service) {
        emit LogoutRequested(service);
    };

    m_dispatcher->OnGaplessModeChanged = [this](bool isGapless) {
        if (OnGaplessModeChanged) OnGaplessModeChanged(isGapless);
    };

    m_dispatcher->OnQuitRequested = [this]() {
        Logger::Log(LogLevel::INFO, ">>> ConsoleController: m_dispatcher->OnQuitRequested triggered! <<<");
        m_audio.Pause();
        emit QuitRequested();
    };

    m_dispatcher->OnSelectPlaylistRequested = [this](const Track& trackToAdd) {
        m_pendingTrackToAdd = trackToAdd;
        m_cachedPlaylists = m_dbManager.GetPlaylists();

        std::string menu = "=== Добавить в плейлист: " + trackToAdd.artist + " - " + trackToAdd.title;
        if (!trackToAdd.source.empty()) menu += " [" + trackToAdd.source + "]";
        menu += " ===\n";

        if (m_cachedPlaylists.empty()) {
            menu += "\n  (Пока нет созданных плейлистов)\n";
        } else {
            menu += "\n";
            for (size_t i = 0; i < m_cachedPlaylists.size(); ++i) {
                menu += "  [" + std::to_string(i + 1) + "] " + m_cachedPlaylists[i].name
                      + " (" + std::to_string(m_cachedPlaylists[i].trackCount) + " треков)\n";
            }
        }
        menu += "\n  [+] Создать новый плейлист\n  [0] Отмена\n\nВыберите номер: ";
        m_renderer->SetOverlay(menu);
        m_currentState = ConsoleState::SELECT_PLAYLIST;
    };

    m_dispatcher->OnSelectPlaylistToPlayRequested = [this]() {
        m_cachedPlaylists = m_dbManager.GetPlaylists();
        if (m_cachedPlaylists.empty()) {
            m_renderer->SetStatusMessage("[Плейлисты] Нет сохраненных плейлистов. Создай через: pl <название>");
            return;
        }

        std::string menu = "=== Выберите плейлист для воспроизведения ===\n\n";
        for (size_t i = 0; i < m_cachedPlaylists.size(); ++i) {
            menu += "  [" + std::to_string(i + 1) + "] " + m_cachedPlaylists[i].name
                  + " (" + std::to_string(m_cachedPlaylists[i].trackCount) + " треков)\n";
        }
        menu += "\n  [0] Отмена\n\nВыберите номер: ";
        m_renderer->SetOverlay(menu);
        m_currentState = ConsoleState::SELECT_PLAYLIST_TO_PLAY;
    };

    // Подписка на ошибки логгера для вывода в строку состояния
    QPointer<ConsoleController> safeThis(this);
    Logger::SetLogCallback([safeThis](LogLevel level, const std::string& message) {
        if (!safeThis) return;
        if (level == LogLevel::ERROR) {
            std::string statusMsg = message;
            if (statusMsg.find("[Ошибка]") == std::string::npos && statusMsg.find("[ERROR]") == std::string::npos) {
                statusMsg = "[Ошибка] " + statusMsg;
            }
            safeThis->SetStatusMessage(statusMsg);
        }
    });

    // Таймер для отрисовки интерфейса (Главный поток)
    m_uiTimer = new QTimer(this);
    m_uiTimer->setTimerType(Qt::PreciseTimer);
    connect(m_uiTimer, &QTimer::timeout, this, &ConsoleController::OnUiTick);
}

ConsoleController::~ConsoleController() {
    Logger::SetLogCallback(nullptr);
    Stop();
}

void ConsoleController::SetState(ConsoleState state) {
    m_currentState = state;
}

void ConsoleController::Start() {
    if (m_isRunning) return;

    std::cout << "\033[?1049h\033[2J\033[999;1H> ";
    std::cout.flush();

    m_isRunning = true;
    m_inputAlive = std::make_shared<std::atomic<bool>>(true);
    m_uiTimer->start(16); // Запуск визуализатора

    // Запускаем фоновый поток только для чтения клавиатуры!
    auto isAlive = m_inputAlive;
    m_inputThread = std::thread([this, isAlive]() {
        InputLoop(isAlive);
    });
}

void ConsoleController::Stop() {
    Logger::Log(LogLevel::INFO, "ConsoleController: Stop() called.");
    if (!m_isRunning) return;
    m_isRunning = false;

    if (m_inputAlive) {
        m_inputAlive->store(false);
    }

    m_uiTimer->stop();

    std::cout << "\033[?1049l\033[?25h";
    std::cout.flush();

#ifdef _WIN32
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin != INVALID_HANDLE_VALUE) {
        // Будим блокирующий getline симуляцией нажатия Enter без вызова опасного CancelIoEx
        INPUT_RECORD ir[2];
        ZeroMemory(ir, sizeof(ir));
        ir[0].EventType = KEY_EVENT;
        ir[0].Event.KeyEvent.bKeyDown = TRUE;
        ir[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        ir[0].Event.KeyEvent.wVirtualScanCode = 0x1C;
        ir[0].Event.KeyEvent.uChar.UnicodeChar = L'\r';
        ir[0].Event.KeyEvent.wRepeatCount = 1;

        ir[1].EventType = KEY_EVENT;
        ir[1].Event.KeyEvent.bKeyDown = FALSE;
        ir[1].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        ir[1].Event.KeyEvent.wVirtualScanCode = 0x1C;
        ir[1].Event.KeyEvent.uChar.UnicodeChar = 0;
        ir[1].Event.KeyEvent.wRepeatCount = 1;

        DWORD written = 0;
        WriteConsoleInputW(hStdin, ir, 2, &written);
    }
#endif

    // Гарантированно дожидаемся завершения потока
    if (m_inputThread.joinable()) {
        m_inputThread.join();
    }
}

void ConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
    if (m_dispatcher) m_dispatcher->SetCurrentProvider(provider);
}

void ConsoleController::SetSourceRouter(SourceRouter* router) {
    if (m_dispatcher) m_dispatcher->SetSourceRouter(router);
}

void ConsoleController::SetStatusMessage(const std::string& msg) {
    if (m_renderer) {
        m_renderer->SetStatusMessage(msg);
    }
}

void ConsoleController::InputLoop(std::shared_ptr<std::atomic<bool>> isAlive) {
    std::string rawInput;
    while (isAlive && isAlive->load() && m_isRunning) {
        // БЛОКИРУЮЩИЙ ВЫЗОВ (Теперь он не мешает графике, так как живет в своем потоке)
        if (!std::getline(std::cin, rawInput)) {
            std::cin.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (!isAlive || !isAlive->load() || !m_isRunning) {
            break;
        }

        // БЕЗОПАСНО ПЕРЕДАЕМ КОМАНДУ В ГЛАВНЫЙ ПОТОК (Qt Event Loop) с защитой через QPointer
        QPointer<ConsoleController> safeThis(this);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, input = std::move(rawInput)]() {
            if (!safeThis || !safeThis->m_isRunning) return;
            safeThis->ProcessInput(input);
        }, Qt::QueuedConnection);
    }
}

void ConsoleController::ProcessInput(const std::string& rawInput) {
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
        if (start == std::string::npos) return;

        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

        m_renderer->SetOverlay("");
        m_renderer->RequestFullRedraw();

        if (input == "0" || input == "q" || input == "Q") {
            m_renderer->SetStatusMessage("[Источник] Выбор отменен.");
            m_currentState = ConsoleState::COMMAND_MODE;
            return;
        }

        if (input == "1") emit SourceChanged("VK");
        else if (input == "2") emit SourceChanged("Spotify");
        else if (input == "3") emit SourceChanged("SoundCloud");
        else if (input == "4") emit SourceChanged("Yandex");
        else if (input == "5") emit SourceChanged("YouTube");
        else if (input == "6") emit SourceChanged("Offline");
        else if (input == "7") emit SourceChanged("All");
        else if (input == "8") {
            m_cachedPlaylists = m_dbManager.GetPlaylists();
            if (m_cachedPlaylists.empty()) {
                m_renderer->SetStatusMessage("[Плейлисты] Нет сохраненных плейлистов. Создай через: pl <название>");
                m_currentState = ConsoleState::COMMAND_MODE;
                return;
            }

            std::string menu = "=== Выберите плейлист для воспроизведения ===\n\n";
            for (size_t i = 0; i < m_cachedPlaylists.size(); ++i) {
                menu += "  [" + std::to_string(i + 1) + "] " + m_cachedPlaylists[i].name
                      + " (" + std::to_string(m_cachedPlaylists[i].trackCount) + " треков)\n";
            }
            menu += "\n  [0] Отмена\n\nВыберите номер: ";
            m_renderer->SetOverlay(menu);
            m_currentState = ConsoleState::SELECT_PLAYLIST_TO_PLAY;
            return;
        }
        else {
            m_renderer->SetStatusMessage("[Ошибка] Неверный номер источника.");
        }

        m_currentState = ConsoleState::COMMAND_MODE;
        return;
    }

    if (m_currentState == ConsoleState::SELECT_PLAYLIST) {
        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return;

        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

        if (input == "0" || input == "q" || input == "Q") {
            m_renderer->SetOverlay("");
            m_renderer->RequestFullRedraw();
            m_renderer->SetStatusMessage("[Плейлисты] Добавление отменено.");
            m_currentState = ConsoleState::COMMAND_MODE;
            return;
        }

        if (input == "+") {
            m_currentState = ConsoleState::CREATE_PLAYLIST_NAME;
            std::string prompt = "=== Создание нового плейлиста ===\n\nВведите название нового плейлиста:\n(или введите 0 для отмены)\n";
            m_renderer->SetOverlay(prompt);
            return;
        }

        m_renderer->SetOverlay("");
        m_renderer->RequestFullRedraw();

        try {
            int idx = std::stoi(input) - 1;
            if (idx >= 0 && idx < static_cast<int>(m_cachedPlaylists.size())) {
                const auto& pl = m_cachedPlaylists[idx];
                m_dbManager.AddTrackToPlaylist(pl.id, m_pendingTrackToAdd.id);
                m_renderer->SetStatusMessage("[Плейлисты] Трек добавлен в '" + pl.name + "'");
            } else {
                m_renderer->SetStatusMessage("[Ошибка] Неверный номер плейлиста.");
            }
        } catch (...) {
            m_renderer->SetStatusMessage("[Ошибка] Неверный ввод.");
        }

        m_currentState = ConsoleState::COMMAND_MODE;
        return;
    }

    if (m_currentState == ConsoleState::SELECT_PLAYLIST_TO_PLAY) {
        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return;

        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);
        m_renderer->SetOverlay("");
        m_renderer->RequestFullRedraw();

        if (input == "0" || input == "q" || input == "Q") {
            m_renderer->SetStatusMessage("[Плейлисты] Выбор отменен.");
            m_currentState = ConsoleState::COMMAND_MODE;
            return;
        }

        try {
            int idx = std::stoi(input) - 1;
            if (idx >= 0 && idx < static_cast<int>(m_cachedPlaylists.size())) {
                const auto& pl = m_cachedPlaylists[idx];
                emit SourceChanged("Custom:" + pl.name);
                m_renderer->SetStatusMessage("[Плейлисты] Воспроизведение плейлиста '" + pl.name + "'");
            } else {
                m_renderer->SetStatusMessage("[Ошибка] Неверный номер плейлиста.");
            }
        } catch (...) {
            m_renderer->SetStatusMessage("[Ошибка] Неверный ввод.");
        }

        m_currentState = ConsoleState::COMMAND_MODE;
        return;
    }

    if (m_currentState == ConsoleState::CREATE_PLAYLIST_NAME) {
        size_t start = rawInput.find_first_not_of(" \t\r\n");
        std::string plName = (start != std::string::npos) ? rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1) : "";

        m_renderer->SetOverlay("");
        m_renderer->RequestFullRedraw();

        if (!plName.empty() && plName != "0") {
            if (m_dbManager.CreatePlaylist(plName)) {
                auto pls = m_dbManager.GetPlaylists();
                for (const auto& p : pls) {
                    if (p.name == plName) {
                        m_dbManager.AddTrackToPlaylist(p.id, m_pendingTrackToAdd.id);
                        break;
                    }
                }
                m_renderer->SetStatusMessage("[Плейлисты] Создан плейлист '" + plName + "' и добавлен трек.");
            } else {
                m_renderer->SetStatusMessage("[Ошибка] Не удалось создать плейлист '" + plName + "'.");
            }
        } else {
            m_renderer->SetStatusMessage("[Плейлисты] Создание отменено.");
        }

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
}

void ConsoleController::OnUiTick() {
    if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
        return;
    }

    m_renderer->Render();

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    int fps = m_renderer->GetFramerate();

    if (fps < 1) fps = 1;

    int newInterval = (fps >= 1000) ? 0 : (1000 / fps);

    if (m_uiTimer->interval() != newInterval) {
        m_uiTimer->setInterval(newInterval);
    }
}