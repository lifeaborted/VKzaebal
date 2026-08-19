#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <cmath>

CommandDispatcher::CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                                     DatabaseManager& db, TrackDownloader& downloader, LyricsFetcher& lyrics)
    : m_audio(audio), m_playlist(playlist), m_dbManager(db), m_downloader(downloader), m_lyricsFetcher(lyrics) {
    RegisterCommands();
}

void CommandDispatcher::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void CommandDispatcher::SetPrintCallback(std::function<void(const std::string&)> printCb) {
    m_printCb = printCb;
}

void CommandDispatcher::Print(const std::string& msg) {
    if (m_printCb) m_printCb(msg);
}

void CommandDispatcher::Dispatch(const std::string& input) {
    if (input.empty()) return;

    std::string lowerInput = input;
    for (char& c : lowerInput) c = std::tolower(c);

    std::string cmd;
    std::string arg;
    size_t spacePos = lowerInput.find(' ');
    
    if (spacePos != std::string::npos) {
        cmd = lowerInput.substr(0, spacePos);
        arg = lowerInput.substr(spacePos + 1);
    } else {
        cmd = lowerInput;
    }

    auto it = m_commands.find(cmd);
    if (it != m_commands.end()) {
        it->second(arg);
    } else {
        Print("[Ошибка] Неизвестная команда. Введи 'h' для справки.\n\n> ");
    }
}

void CommandDispatcher::RegisterCommands() {
    m_commands["p"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume();
        }, Qt::QueuedConnection);
    };

    m_commands["n"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.Next(); }, Qt::QueuedConnection);
    };

    m_commands["b"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.Previous(); }, Qt::QueuedConnection);
    };

    m_commands["+"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection);
    };

    m_commands["-"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection);
    };

    m_commands["v"] = [this](const std::string& arg) {
        try {
            int vol = std::stoi(arg);
            if (vol < 0) vol = 0;
            if (vol > 100) vol = 100;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, vol]() {
                m_audio.SetVolume(vol / 100.0f);
            }, Qt::QueuedConnection);
            Print("[Громкость] Установлена громкость: " + std::to_string(vol) + "%\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ");
        }
    };

    m_commands["seek"] = [this](const std::string& arg) {
        try {
            double pos = std::stod(arg);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, pos]() {
                m_audio.SetPositionSeconds(pos);
            }, Qt::QueuedConnection);
            Print("[Перемотка] Переход на " + std::to_string(static_cast<int>(pos)) + " сек.\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: seek <секунды>\n\n> ");
        }
    };

    m_commands["source"] = [this](const std::string&) {
        Print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - Оффлайн режим\n\nВведите номер: ");
        if (OnSourceChangeRequested) OnSourceChangeRequested("SELECT");
    };

    m_commands["vis"] = [this](const std::string&) {
        if (OnVisualizerToggled) OnVisualizerToggled();
    };

    m_commands["q"] = [this](const std::string&) {
        if (OnQuitRequested) OnQuitRequested();
    };

    m_commands["h"] = [this](const std::string&) {
        std::string s(50, '*');
        std::string helpText = "\n" + s + "\n"
                  + " [P] Play/Pause\n [N] Next\n [B] Prev\n"
                  + " [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n"
                  + " [seek <num>] Seek to seconds\n"
                  + " [source] Select audio source\n"
                  + " [vis] Toggle visualizer\n"
                  + " [Q] Quit\n"
                  + s + "\n\n> ";
        Print(helpText);
    };
}