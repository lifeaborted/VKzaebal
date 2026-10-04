#include "LightConsoleController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

LightConsoleController::LightConsoleController(
    IAudioEngine& audio,
    PlaylistManager& playlist,
    DatabaseManager& dbManager,
    TrackDownloader& downloader,
    LyricsFetcher& lyricsFetcher,
    QNetworkAccessManager* networkManager,
    QObject* parent)
    : IUiController(parent),
      m_audio(audio),
      m_playlist(playlist),
      m_dbManager(dbManager),
      m_downloader(downloader),
      m_lyricsFetcher(lyricsFetcher) {
    Q_UNUSED(networkManager);
}

LightConsoleController::~LightConsoleController() {
    Stop();
}

void LightConsoleController::Start() {
    if (m_isRunning) return;
    m_isRunning = true;

    std::cout << "\n======================================================\n";
    std::cout << "  VK Audio Player — Минимальный консольный режим (CLI-Light)\n";
    std::cout << "  Введите 'help' или '?' для списка доступных команд.\n";
    std::cout << "======================================================\n\n";
    std::cout.flush();

    m_inputThread = std::thread(&LightConsoleController::InputLoop, this);
}

void LightConsoleController::Stop() {
    if (!m_isRunning) return;
    m_isRunning = false;

    if (m_inputThread.joinable()) {
        m_inputThread.detach();
    }
}

void LightConsoleController::SetStatusMessage(const std::string& msg) {
    std::cout << "\n" << msg << "\n";
    PrintPrompt();
}

void LightConsoleController::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void LightConsoleController::SetWaitingAuth(bool waiting) {
    m_isWaitingAuth = waiting;
    if (waiting) {
        std::cout << "\n[Авторизация] Требуется авторизация в сервисе...\n";
        PrintPrompt();
    }
}

void LightConsoleController::OnTrackChanged(const Track& track) {
    std::cout << "\n[▶ Сейчас играет] " << track.artist << " — " << track.title;
    if (track.duration > 0) {
        int m = track.duration / 60;
        int s = track.duration % 60;
        std::cout << " (" << m << ":" << (s < 10 ? "0" : "") << s << ")";
    }
    std::cout << "\n";
    PrintPrompt();
}

void LightConsoleController::OnAudioFetched(const std::vector<Track>& tracks) {
    std::cout << "\n[Синхронизация] Получено треков: " << tracks.size() << "\n";
    PrintPrompt();
}

void LightConsoleController::OnFinishedFetching() {
    std::cout << "\n[Синхронизация] Фоновая синхронизация треков завершена.\n";
    PrintPrompt();
}

void LightConsoleController::PrintPrompt() {
    std::cout << "cli-light> ";
    std::cout.flush();
}

void LightConsoleController::PrintHelp() {
    std::cout << "\n--- Доступные команды CLI-Light ---\n";
    std::cout << "  p, play, pause       - Пауза / Возобновление\n";
    std::cout << "  n, next              - Следующий трек\n";
    std::cout << "  b, prev              - Предыдущий трек\n";
    std::cout << "  np, info             - Информация о текущем треке\n";
    std::cout << "  ls, list             - Список треков в очереди\n";
    std::cout << "  v <0-100>            - Установить громкость (в %)\n";
    std::cout << "  +, -                 - Громкость +5% / -5%\n";
    std::cout << "  src <имя>            - Переключить сервис (VK, Yandex, etc.)\n";
    std::cout << "  q, quit, exit        - Выход из плеера\n";
    std::cout << "-----------------------------------\n";
}

void LightConsoleController::PrintNowPlaying() {
    Track cur = m_playlist.GetCurrentTrack();
    if (cur.id.empty()) {
        std::cout << "\n[Инфо] Сейчас ничего не играет.\n";
        return;
    }
    double pos = m_audio.GetPositionSeconds();
    double len = m_audio.GetLengthSeconds();
    if (len <= 0.0) len = static_cast<double>(cur.duration);

    int posM = static_cast<int>(pos) / 60;
    int posS = static_cast<int>(pos) % 60;
    int lenM = static_cast<int>(len) / 60;
    int lenS = static_cast<int>(len) % 60;

    std::string state = m_audio.IsPlaying() ? "Играет" : "Пауза";
    int vol = static_cast<int>(m_audio.GetVolume() * 100.0f);

    std::cout << "\n--- Сейчас играет [" << state << "] ---\n";
    std::cout << "  Трек:      " << cur.artist << " — " << cur.title << "\n";
    std::cout << "  Позиция:   " << posM << ":" << (posS < 10 ? "0" : "") << posS
              << " / " << lenM << ":" << (lenS < 10 ? "0" : "") << lenS << "\n";
    std::cout << "  Громкость: " << vol << "%\n";
    std::cout << "  Источник:  " << (cur.source.empty() ? "VK" : cur.source) << "\n";
    std::cout << "------------------------------------\n";
}

void LightConsoleController::PrintQueue() {
    size_t count = m_playlist.GetQueueSize();
    if (count == 0) {
        std::cout << "\n[Очередь] Очередь воспроизведения пуста.\n";
        return;
    }
    int curIdx = m_playlist.GetCurrentQueueIndex();
    std::cout << "\n--- Очередь воспроизведения (" << count << " треков) ---\n";
    size_t start = (curIdx > 3) ? static_cast<size_t>(curIdx - 3) : 0;
    size_t end = std::min(count, start + 10);
    auto slice = m_playlist.GetQueueSlice(static_cast<int>(start), static_cast<int>(end - start));

    for (size_t i = 0; i < slice.size(); ++i) {
        size_t realIdx = start + i;
        bool isCur = (static_cast<int>(realIdx) == curIdx);
        std::cout << (isCur ? " ▶ " : "   ")
                  << std::setw(3) << (realIdx + 1) << ". "
                  << slice[i].artist << " — " << slice[i].title << "\n";
    }
    if (end < count) {
        std::cout << "   ... и еще " << (count - end) << " треков\n";
    }
    std::cout << "--------------------------------------------\n";
}

void LightConsoleController::InputLoop() {
    std::string line;
    PrintPrompt();

    while (m_isRunning) {
        if (!std::getline(std::cin, line)) {
            break;
        }

        // Trim whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();

        if (line.empty()) {
            PrintPrompt();
            continue;
        }

        QMetaObject::invokeMethod(this, [this, line]() {
            ProcessCommand(line);
            PrintPrompt();
        }, Qt::QueuedConnection);
    }
}

void LightConsoleController::ProcessCommand(const std::string& cmd) {
    if (cmd == "q" || cmd == "quit" || cmd == "exit") {
        std::cout << "[Выход] Завершение работы...\n";
        emit QuitRequested();
        return;
    }

    if (cmd == "help" || cmd == "h" || cmd == "?") {
        PrintHelp();
        return;
    }

    if (cmd == "p" || cmd == "play" || cmd == "pause" || cmd == " ") {
        if (m_audio.IsPlaying()) {
            m_audio.Pause();
            std::cout << "[Пауза]\n";
        } else {
            m_audio.Resume();
            std::cout << "[Воспроизведение]\n";
        }
        return;
    }

    if (cmd == "n" || cmd == "next") {
        m_playlist.Next();
        return;
    }

    if (cmd == "b" || cmd == "prev") {
        m_playlist.Previous();
        return;
    }

    if (cmd == "np" || cmd == "info") {
        PrintNowPlaying();
        return;
    }

    if (cmd == "ls" || cmd == "list") {
        PrintQueue();
        return;
    }

    if (cmd == "+") {
        float v = std::clamp(m_audio.GetVolume() + 0.05f, 0.0f, 1.0f);
        m_audio.SetVolume(v);
        std::cout << "[Громкость] " << static_cast<int>(v * 100.0f) << "%\n";
        return;
    }

    if (cmd == "-") {
        float v = std::clamp(m_audio.GetVolume() - 0.05f, 0.0f, 1.0f);
        m_audio.SetVolume(v);
        std::cout << "[Громкость] " << static_cast<int>(v * 100.0f) << "%\n";
        return;
    }

    if (cmd.rfind("v ", 0) == 0 || cmd.rfind("vol ", 0) == 0) {
        size_t sp = cmd.find(' ');
        try {
            int val = std::stoi(cmd.substr(sp + 1));
            float v = std::clamp(static_cast<float>(val) / 100.0f, 0.0f, 1.0f);
            m_audio.SetVolume(v);
            std::cout << "[Громкость] " << static_cast<int>(v * 100.0f) << "%\n";
        } catch (...) {
            std::cout << "[Ошибка] Укажите число от 0 до 100\n";
        }
        return;
    }

    if (cmd.rfind("src ", 0) == 0) {
        std::string src = cmd.substr(4);
        std::cout << "[Источник] Переключение на: " << src << "\n";
        emit SourceChanged(src);
        return;
    }

    // ==============================================================================
    // [МЕСТО ДЛЯ КАСТОМИЗАЦИИ ПОЛЬЗОВАТЕЛЕМ]
    // Сюда можно добавить любые свои минимальные команды и функционал:
    // ==============================================================================
    std::cout << "[Неизвестная команда] '" << cmd << "'. Введите 'help' для справки.\n";
}
