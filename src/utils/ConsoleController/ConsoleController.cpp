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
}

void ConsoleController::Stop() {
    m_isRunning = false;
    if (m_inputThread.joinable()) {
        // Заглушка, чтобы разбудить std::getline (в реальном приложении можно использовать select/poll для консоли, 
        // но для наших целей достаточно простого флага)
        m_inputThread.detach(); 
    }
}

void ConsoleController::InputLoop() {
    std::string input;
    
    while (m_isRunning) {
        std::getline(std::cin, input);
        if (input.empty()) continue;

        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            QString urlStr = QString::fromStdString(input);
            std::cout << "Обработка ссылки...\n";

            // Прокидываем ссылку в AuthManager через главный поток Qt
            QMetaObject::invokeMethod(&m_authManager, [&, urlStr]() {
                m_authManager.onUrlIntercepted(urlStr);
            }, Qt::QueuedConnection);

            m_currentState = ConsoleState::COMMAND_MODE;
            continue;
        }

        if (m_currentState == ConsoleState::COMMAND_MODE) {
            // Громкость
            if (input.length() >= 3 && std::tolower(input[0]) == 'v' && input[1] == ' ') {
                try {
                    int volTarget = std::stoi(input.substr(2));
                    if (volTarget >= 0 && volTarget <= 100) {
                        float normalizedVol = volTarget / 100.0f;
                        QMetaObject::invokeMethod(QCoreApplication::instance(), [&, normalizedVol]() {
                            m_audio.SetVolume(normalizedVol);
                        }, Qt::QueuedConnection);
                    } else {
                        std::cout << "[Ошибка] Введите значение от 0 до 100 (например: v 50)\n> ";
                    }
                } catch (...) {
                    std::cout << "[Ошибка] Неверный формат числа.\n> ";
                }
                continue;
            }

            // Прыжок к треку
            if (input.length() >= 3 && std::tolower(input[0]) == 'j' && input[1] == ' ') {
                try {
                    int idx = std::stoi(input.substr(2));
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, idx]() { 
                        m_playlist.JumpTo(idx - 1); 
                    }, Qt::QueuedConnection);
                } catch (...) {
                    std::cout << "[Ошибка] Неверный номер трека.\n> ";
                }
                continue;
            }

            // Базовые команды
            char command = std::tolower(input[0]);
            switch (command) {
                case 'p': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume(); }, Qt::QueuedConnection); break;
                case 'n': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Next(); }, Qt::QueuedConnection); break;
                case 'b': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Previous(); }, Qt::QueuedConnection); break;
                case '+': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                case '-': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                case 's': 
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { 
                        m_playlist.ToggleShuffle();

                        // Сохраняем новую очередь в БД и выгружаем в TXT
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