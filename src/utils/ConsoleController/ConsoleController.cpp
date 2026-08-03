#include "ConsoleController.h"
#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/DatabaseManager/DatabaseManager.h"

#include <iostream>
#include <string>
#include <cctype>
#include <QCoreApplication>
#include <QMetaObject>

ConsoleController::ConsoleController(AudioEngine& audio, PlaylistManager& playlist, VkAuthManager& authManager, DatabaseManager& dbManager, QObject* parent)
    : QObject(parent), m_audio(audio), m_playlist(playlist), m_authManager(authManager), m_dbManager(dbManager),
      m_currentState(ConsoleState::COMMAND_MODE), m_isRunning(false) {}

ConsoleController::~ConsoleController() {
    Stop();
}

void ConsoleController::SetState(ConsoleState state) {
    m_currentState = state;
}

void ConsoleController::Start() {
    if (m_isRunning) return;
    m_isRunning = true;
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
    m_uiThread = std::thread(&ConsoleController::UiLoop, this); // Запускаем UI поток
}

void ConsoleController::Stop() {
    m_isRunning = false;
    if (m_inputThread.joinable()) m_inputThread.detach();
    if (m_uiThread.joinable()) m_uiThread.join(); // Гасим UI поток
}

void ConsoleController::InputLoop() {
    std::string rawInput;
    std::string clearLine = "\r                                                                                \r";

    while (m_isRunning) {
        std::getline(std::cin, rawInput);

        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        std::string input = rawInput.substr(start, rawInput.find_last_not_of(" \t\r\n") - start + 1);

        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            QString urlStr = QString::fromStdString(input);
            std::cout << clearLine << "Обработка ссылки...\n";
            std::cout.flush();

            QMetaObject::invokeMethod(&m_authManager, [&, urlStr]() {
                m_authManager.onUrlIntercepted(urlStr);
            }, Qt::QueuedConnection);

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        if (m_currentState == ConsoleState::COMMAND_MODE) {

            // 2. Переводим всю строку в нижний регистр для безопасных проверок команд
            std::string lowerInput = input;
            for (char& c : lowerInput) c = std::tolower(c);

            // --- КОМАНДА: Установка громкости (v <num>) ---
            if (lowerInput.length() >= 2 && lowerInput[0] == 'v' && lowerInput[1] == ' ') {
                try {
                    int vol = std::stoi(input.substr(2));
                    if (vol < 0) vol = 0;
                    if (vol > 100) vol = 100;

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, vol]() {
                        m_audio.SetVolume(vol / 100.0f);
                    }, Qt::QueuedConnection);

                    std::cout << clearLine << "[Громкость] Установлена громкость: " << vol << "%\n> ";
                    std::cout.flush();
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- КОМАНДА: Теку громкость ---
            if (lowerInput == "cv") {
                int vol = static_cast<int>(m_audio.GetVolume() * 100);
                std::cout << clearLine << "[Громкость] Текущая громкость: " << vol << "%\n> ";
                std::cout.flush();
                continue;
            }

            // --- КОМАНДА: Прыжок к треку (j <num>) ---
            if (lowerInput.length() >= 2 && lowerInput[0] == 'j' && lowerInput[1] == ' ') {
                try {
                    int idx = std::stoi(input.substr(2));
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, idx]() {
                        m_playlist.JumpTo(idx - 1);
                    }, Qt::QueuedConnection);
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный номер трека.\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- КОМАНДА: Смена режима перехода (mode <0/1>) ---
            if (lowerInput.length() >= 6 && lowerInput.substr(0, 5) == "mode ") {
                try {
                    int mode = std::stoi(input.substr(5));
                    if (mode == 0 || mode == 1) {
                        bool isGapless = (mode == 1);

                        if (OnGaplessModeChanged) {
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [&, isGapless]() {
                                OnGaplessModeChanged(isGapless);
                            }, Qt::QueuedConnection);
                        }

                        std::cout << clearLine << "[Режим] Установлен " << (isGapless ? "плавный (gapless)" : "стандартный") << " переход.\n> ";
                        std::cout.flush();
                    } else {
                        std::cout << clearLine << "[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n> ";
                        std::cout.flush();
                    }
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // Базовые односимвольные команды
            char command = lowerInput[0];
            std::string s(50, '*');
            switch (command) {
                case 'h':
                    std::cout << clearLine
                              << "\n"
                              <<s<<"\n"
                              << " [P] Play/Pause\n [N] Next\n [B] Prev\n"
                              << " [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n"
                              << " [S] Shuffle\n [R] Repeat Mode\n"
                              << " [J <num>] Jump to track\n [cv] Current volume\n [Q] Quit\n"
                              << " [mode <0/1>] 0 - Standard, 1 - Gapless transition\n"
                              << " [tl] Export tracklist to TXT\n"
                              <<s
                              <<"\n\n> ";
                    std::cout.flush();
                    break;
                case 'tl':
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                    std::cout << clearLine << "[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n> ";
                    std::cout.flush();
                    continue;
                case 'p': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume(); }, Qt::QueuedConnection); break;
                case 'n': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Next(); }, Qt::QueuedConnection); break;
                case 'b': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Previous(); }, Qt::QueuedConnection); break;
                case '+': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                case '-': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                case 's':
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                        m_playlist.ToggleShuffle();
                        m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                        m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                    }, Qt::QueuedConnection);
                    break;
                case 'r': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.ToggleRepeat(); }, Qt::QueuedConnection); break;
                case 'q':
                    emit QuitRequested();
                    m_isRunning = false;
                    return;
            }
        }
    }
}

void ConsoleController::UiLoop() {
    int lastSecond = -1;

    while (m_isRunning) {
        if (m_currentState == ConsoleState::COMMAND_MODE && m_audio.IsPlaying()) {
            double current = m_audio.GetPositionSeconds();
            if (current < 0.0) current = 0.0;

            int currentSecInt = static_cast<int>(current);

            if (currentSecInt != lastSecond) {
                lastSecond = currentSecInt;

                double total = m_audio.GetLengthSeconds();
                if (total <= 0.0) {
                    total = static_cast<double>(m_playlist.GetCurrentTrack().duration);
                }

                if (total > 0.0) {
                    int percent = static_cast<int>((current / total) * 100.0);
                    if (percent > 100) percent = 100;
                    if (percent < 0) percent = 0;

                    int curMin = currentSecInt / 60;
                    int curSec = currentSecInt % 60;
                    int totMin = static_cast<int>(total) / 60;
                    int totSec = static_cast<int>(total) % 60;

                    int barLength = 50;
                    int filled = static_cast<int>((current / total) * barLength);
                    if (filled > barLength) filled = barLength;
                    if (filled < 0) filled = 0;

                    std::string bar = "[";
                    for (int i = 0; i < barLength; ++i) {
                        if (i < filled) bar += "\xE2\x96\x88";
                        else bar += "-";
                    }
                    bar += "]";

                    printf("\r%02d:%02d / %02d:%02d %s %d%%          ",
                           curMin, curSec, totMin, totSec, bar.c_str(), percent);
                    fflush(stdout);
                }
            }
        } else {
            lastSecond = -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}