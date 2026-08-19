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
#include <QSettings>

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
    // --- Управление воспроизведением ---
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

    m_commands["cv"] = [this](const std::string&) {
        int vol = static_cast<int>(std::round(m_audio.GetVolume() * 100));
        Print("[Громкость] Текущая громкость: " + std::to_string(vol) + "%\n\n> ");
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

    m_commands["r"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() { m_playlist.ToggleRepeat(); }, Qt::QueuedConnection);
    };

    // --- Навигация и Плейлист ---
    m_commands["j"] = [this](const std::string& arg) {
        try {
            int idx = std::stoi(arg);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, idx]() {
                m_playlist.JumpToQueueIndex(idx - 1);
            }, Qt::QueuedConnection);
            Print("[Плейлист] Переход к треку " + std::to_string(idx) + "\n\n> ");
        } catch (...) {
            Print("[Ошибка] Неверный номер трека.\n\n> ");
        }
    };

    m_commands["tl"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
    };

    m_commands["sh"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(true);
            m_playlist.JumpToQueueIndex(0);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Плейлист] Режим: Перемешивание (Shuffle). Стартуем случайный трек!\n\n> ");
    };

    m_commands["st"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(false);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Плейлист] Режим: Стандартный порядок\n\n> ");
    };

    auto resetHandler = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.SetShuffle(false);
            m_playlist.JumpTo(0);
            std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
            m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), src, m_playlist.IsShuffle());
            m_dbManager.ExportQueueToTxt(m_playlist.GetQueueTracks(), "playlist.txt", m_playlist.IsShuffle());
        }, Qt::QueuedConnection);
        Print("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
    };
    m_commands["rs"] = resetHandler;
    m_commands["reset"] = resetHandler;

    m_commands["search"] = [this](const std::string& arg) {
        if (arg.empty()) {
            Print("[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ");
            return;
        }

        std::string searchArtist = arg;
        std::string searchTitle = arg;
        bool isSplit = false;

        size_t dashPos = arg.find("-");
        if (dashPos != std::string::npos) {
            searchArtist = arg.substr(0, dashPos);
            searchTitle = arg.substr(dashPos + 1);

            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t") + 1);
            };
            trim(searchArtist);
            trim(searchTitle);
            isSplit = true;
        }

        QString qArtist = QString::fromStdString(searchArtist).trimmed();
        QString qTitle = QString::fromStdString(searchTitle).trimmed();
        QString qFull = QString::fromStdString(arg).trimmed();

        std::vector<Track> queue = m_playlist.GetQueueTracks();
        std::string s(50, '-');
        std::string res = "[Поиск] Результаты по запросу \"" + qFull.toStdString() + "\":\n" + s + "\n";

        int matchCount = 0;
        for (size_t i = 0; i < queue.size(); ++i) {
            QString trackArtist = QString::fromStdString(queue[i].artist);
            QString trackTitle = QString::fromStdString(queue[i].title);

            bool match = false;
            if (isSplit) {
                match = trackArtist.contains(qArtist, Qt::CaseInsensitive) && trackTitle.contains(qTitle, Qt::CaseInsensitive);
            } else {
                match = trackArtist.contains(qFull, Qt::CaseInsensitive) || trackTitle.contains(qFull, Qt::CaseInsensitive);
            }

            if (match) {
                res += "[" + std::to_string(i + 1) + "]. " + queue[i].artist + " - " + queue[i].title + " [" + queue[i].GetFormattedDuration() + "]\n";
                matchCount++;
                if (matchCount >= 20) {
                    res += "... Показаны первые 20 совпадений.\n";
                    break;
                }
            }
        }

        if (matchCount == 0) res += "Ничего не найдено.\n";
        res += s + "\n\n> ";
        Print(res);
    };

    // --- Сеть, Загрузки и Тексты ---
    auto dlRmHandler = [this](const std::string& cmd, const std::string& arg) {
        Track targetTrack;
        bool isValid = false;

        if (arg.empty()) {
            targetTrack = m_playlist.GetCurrentTrack();
            isValid = true;
        } else {
            try {
                int idx = std::stoi(arg) - 1;
                std::vector<Track> queue = m_playlist.GetQueueTracks();
                if (idx >= 0 && idx < queue.size()) {
                    targetTrack = queue[idx];
                    isValid = true;
                }
            } catch(...) {}
        }

        if (isValid && !targetTrack.id.empty()) {
            QString pathMp3 = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "mp3");
            QString pathAac = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "aac");

            if (cmd == "dl") {
                if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                    Print("[Загрузка] Трек уже скачан.\n\n> ");
                } else {
                    if (!m_currentProvider) {
                        Print("[Ошибка] Нет активного онлайн-источника для скачивания.\n\n> ");
                        return;
                    }
                    Print("[Загрузка] Получение ссылки для " + targetTrack.title + "...\n\n> ");
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, targetTrack]() {
                        m_currentProvider->FetchTrackUrl(targetTrack.id, [this, targetTrack](const std::string& url, bool err) {
                            if (!err && !url.empty()) {
                                m_downloader.Download(targetTrack, url);
                            } else {
                                Logger::Log(LogLevel::WARNING, "Failed to get URL for download.");
                            }
                        });
                    }, Qt::QueuedConnection);
                }
            } else if (cmd == "rm") {
                if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                    QFile::remove(pathMp3);
                    QFile::remove(pathAac);
                    Print("[Кэш] Удален: " + targetTrack.artist + " - " + targetTrack.title + "\n\n> ");
                } else {
                    Print("[Кэш] Трек не был скачан.\n\n> ");
                }
            }
        } else {
            Print("[Ошибка] Не удалось найти трек.\n\n> ");
        }
    };
    m_commands["dl"] = [=](const std::string& arg) { dlRmHandler("dl", arg); };
    m_commands["rm"] = [=](const std::string& arg) { dlRmHandler("rm", arg); };

    auto lyricsHandler = [this](const std::string& arg) {
        bool isNewFile = (arg.find("new") != std::string::npos);
        Track currentTrack = m_playlist.GetCurrentTrack();

        auto showLyricsFile = [this, currentTrack, isNewFile](const std::string& text) {
            if (text.empty()) {
                Print("[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ");
                return;
            }

            QString filePath = PathManager::GetLyricsFilePath(currentTrack.artist, currentTrack.title, isNewFile);
            QFile file(filePath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(QByteArray::fromStdString(text));
                file.close();
            }

            QFileInfo fileInfo(filePath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            Print("[Текст] Открыт файл: " + filePath.toStdString() + "\n\n> ");
        };

        std::string text = currentTrack.lyrics;
        if (text.empty()) {
            Print("[Текст] Поиск текста...\n\n> ");
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, currentTrack, showLyricsFile]() {
                m_lyricsFetcher.FetchLyrics(currentTrack.artist, currentTrack.title, [this, currentTrack, showLyricsFile](const std::string& fetchedText) {
                    if (!fetchedText.empty()) {
                        m_dbManager.UpdateTrackLyrics(currentTrack.id, fetchedText);
                    }
                    showLyricsFile(fetchedText);
                });
            }, Qt::QueuedConnection);
        } else {
            showLyricsFile(text);
        }
    };
    m_commands["ly"] = lyricsHandler;
    m_commands["lyrics"] = lyricsHandler;

    // --- Настройки системы ---
    m_commands["source"] = [this](const std::string&) {
        Print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - SoundCloud\n4 - Оффлайн режим\n\nВведите номер: ");
        if (OnSourceChangeRequested) OnSourceChangeRequested("SELECT");
    };

    m_commands["vis"] = [this](const std::string&) {
        if (OnVisualizerToggled) OnVisualizerToggled();
    };

    m_commands["mode"] = [this](const std::string& arg) {
        try {
            int mode = std::stoi(arg);
            if (mode == 0 || mode == 1) {
                bool isGapless = (mode == 1);
                if (OnGaplessModeChanged) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, isGapless]() {
                        OnGaplessModeChanged(isGapless);
                    }, Qt::QueuedConnection);
                }
                std::string modeStr = isGapless ? "плавный (gapless)" : "стандартный";
                Print("[Режим] Установлен " + modeStr + " переход.\n\n> ");
            } else {
                Print("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
            }
        } catch (...) {
            Print("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
        }
    };

    m_commands["i"] = [this](const std::string&) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            Track current = m_playlist.GetCurrentTrack();
            std::string info = "[Инфо] Артист: " + current.artist + "\n"
                             + "[Инфо] Название: " + current.title + "\n"
                             + "[Инфо] ID: " + current.id + "\n"
                             + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
            Print(info);
        }, Qt::QueuedConnection);
    };

    m_commands["q"] = [this](const std::string&) {
        if (OnQuitRequested) OnQuitRequested();
    };

    m_commands["h"] = [this](const std::string&) {
        std::string s(50, '*');
        std::string helpText = "\n" + s + "\n"
                  + " [P] Play/Pause\n [N] Next\n [B] Prev\n"
                  + " [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n"
                  + " [st] Standard Order\n [sh] Shuffle\n [R] Repeat Mode\n"
                  + " [J <num>] Jump to track\n [cv] Current volume\n"
                  + " [rs] Reset Session\n"
                  + " [mode <0/1>] 0 - Standard, 1 - Gapless transition\n"
                  + " [search <text>] Search tracks in playlist\n"
                  + " [ly] Show lyrics for current track\n"
                  + " [source] Select audio source\n"
                  + " [tl] Export tracklist to TXT\n"
                  + " [dl] / [dl <num>] Download track\n"
                  + " [rm] / [rm <num>] Delete downloaded track\n"
                  + " [vis] Toggle visualizer\n"
                  + " [Q] Quit\n"
                  + s + "\n\n> ";
        Print(helpText);
    };
}