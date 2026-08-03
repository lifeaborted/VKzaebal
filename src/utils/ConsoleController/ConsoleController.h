#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <functional>

class AudioEngine;
class PlaylistManager;
class VkAuthManager;
class DatabaseManager; // <--- ДОБАВЛЯЕМ

enum class ConsoleState {
    COMMAND_MODE,
    WAITING_TOKEN_URL
};

class ConsoleController : public QObject {
    Q_OBJECT
public:
    ConsoleController(AudioEngine& audio, PlaylistManager& playlist, VkAuthManager& authManager, DatabaseManager& dbManager, QObject* parent = nullptr);
    ~ConsoleController();

    void Start();
    void Stop();
    void SetState(ConsoleState state);

    signals:
        void QuitRequested();

private:
    void InputLoop();

    AudioEngine& m_audio;
    PlaylistManager& m_playlist;
    VkAuthManager& m_authManager;
    DatabaseManager& m_dbManager;

    std::atomic<ConsoleState> m_currentState;
    std::atomic<bool> m_isRunning;
    std::thread m_inputThread;
};