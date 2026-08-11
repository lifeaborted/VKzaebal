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

#include "ConsoleController.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/auth/Manager.h"
#include "services/database/DatabaseManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"


ConsoleController::ConsoleController(
    IAudioEngine& audio,
    PlaylistManager& playlist,
    Manager& authManager,
    DatabaseManager& dbManager,
    Client& vkClient,
    TrackDownloader& downloader,
    LyricsFetcher& lyricsFetcher,
    QObject* parent
    ):
        QObject(parent),
        m_audio(audio),
        m_playlist(playlist),
        m_authManager(authManager),
        m_dbManager(dbManager),
        m_vkClient(vkClient),
        m_downloader(downloader),
        m_lyricsFetcher(lyricsFetcher),
        m_currentState(ConsoleState::COMMAND_MODE),
        m_isRunning(false) {}

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

    auto syncPrint = [](const std::string& text) {
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
            std::string lowerInput = input;
            for (char& c : lowerInput) c = std::tolower(c);

            std::string cmdType = lowerInput.substr(0, 2);
            if (cmdType == "dl" || cmdType == "rm") {
                Track targetTrack;
                bool isValid = false;

                if (lowerInput == cmdType) {
                    targetTrack = m_playlist.GetCurrentTrack();
                    isValid = true;
                } else if (lowerInput.length() > 3 && lowerInput[2] == ' ') {
                    try {
                        int idx = std::stoi(input.substr(3)) - 1;
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

                    if (cmdType == "dl") {
                        if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                            syncPrint("[Загрузка] Трек уже скачан.\n\n> ");
                        } else {
                            syncPrint("[Загрузка] Получение ссылки для " + targetTrack.title + "...\n\n> ");

                            QMetaObject::invokeMethod(QCoreApplication::instance(), [&, targetTrack]() {
                                m_vkClient.FetchTrackUrl(targetTrack.id, [this, targetTrack](const std::string& url, bool err) {
                                    if (!err && !url.empty()) {
                                        m_downloader.Download(targetTrack, url);
                                    } else {
                                        Logger::Log(LogLevel::WARNING, "Failed to get URL for download.");
                                    }
                                });
                            }, Qt::QueuedConnection);
                        }
                    } else if (cmdType == "rm") {
                        if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                            QFile::remove(pathMp3);
                            QFile::remove(pathAac);
                            syncPrint("[Кэш] Удален: " + targetTrack.artist + " - " + targetTrack.title + "\n\n> ");
                        } else {
                            syncPrint("[Кэш] Трек не был скачан.\n\n> ");
                        }
                    }
                }
                continue;
            }

            if (lowerInput.length() >= 2 && lowerInput[0] == 'v' && lowerInput[1] == ' ') {
                try {
                    int vol = std::stoi(input.substr(2));
                    if (vol < 0) vol = 0;
                    if (vol > 100) vol = 100;

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, vol]() {
                        m_audio.SetVolume(vol / 100.0f);
                    }, Qt::QueuedConnection);

                    syncPrint("[Громкость] Установлена громкость: " + std::to_string(vol) + "%\n\n> ");
                } catch (...) {
                    syncPrint("[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ");
                }
                continue;
            }

            if (lowerInput == "cv") {
                int vol = static_cast<int>(std::round(m_audio.GetVolume() * 100));
                syncPrint("[Громкость] Текущая громкость: " + std::to_string(vol) + "%\n\n> ");
                continue;
            }

            if (lowerInput.length() >= 2 && lowerInput[0] == 'j' && lowerInput[1] == ' ') {
                try {
                    int idx = std::stoi(input.substr(2));
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, idx]() {
                        m_playlist.JumpToQueueIndex(idx - 1);
                    }, Qt::QueuedConnection);
                    syncPrint("[Плейлист] Переход к треку " + std::to_string(idx) + "\n\n> ");
                } catch (...) {
                    syncPrint("[Ошибка] Неверный номер трека.\n\n> ");
                }
                continue;
            }

            if (lowerInput == "tl") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                syncPrint("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
                continue;
            }

            if (lowerInput == "sh") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(true);

                    m_playlist.JumpToQueueIndex(0);

                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                syncPrint("[Плейлист] Режим: Перемешивание (Shuffle). Стартуем случайный трек!\n\n> ");
                continue;
            }

            if (lowerInput == "st") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(false);
                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                syncPrint("[Плейлист] Режим: Стандартный порядок\n\n> ");
                continue;
            }

            if (lowerInput == "rs" || lowerInput == "reset") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(false);
                    m_playlist.JumpTo(0);
                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                syncPrint("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
                continue;
            }

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

                        std::string modeStr = isGapless ? "плавный (gapless)" : "стандартный";
                        syncPrint("[Режим] Установлен " + modeStr + " переход.\n\n> ");
                    } else {
                        syncPrint("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
                    }
                } catch (...) {
                    syncPrint("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
                }
                continue;
            }

            if (lowerInput.length() > 5 && lowerInput.substr(0, 5) == "seek ") {
                try {
                    double pos = std::stod(input.substr(5));

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, pos]() {
                        m_audio.SetPositionSeconds(pos);
                    }, Qt::QueuedConnection);

                    syncPrint("[Перемотка] Переход на " + std::to_string(static_cast<int>(pos)) + " сек.\n\n> ");
                } catch (...) {
                    syncPrint("[Ошибка] Неверный формат. Используй: seek <секунды> (например: seek 65)\n\n> ");
                }
                continue;
            }

            if (lowerInput.length() > 7 && lowerInput.substr(0, 7) == "search ") {
                std::string query = input.substr(7);
                std::string searchArtist = query;
                std::string searchTitle = query;
                bool isSplit = false;

                size_t dashPos = query.find("-");
                if (dashPos != std::string::npos) {
                    searchArtist = query.substr(0, dashPos);
                    searchTitle = query.substr(dashPos + 1);

                    auto trim = [](std::string& s) {
                        s.erase(0, s.find_first_not_of(" \t"));
                        s.erase(s.find_last_not_of(" \t") + 1);
                    };
                    trim(searchArtist);
                    trim(searchTitle);
                    isSplit = true;
                }

                // Переводим в нижний регистр для независимого от регистра поиска
                QString qArtist = QString::fromStdString(searchArtist).trimmed();
                QString qTitle = QString::fromStdString(searchTitle).trimmed();
                QString qFull = QString::fromStdString(query).trimmed();

                if (!qFull.isEmpty()) {
                    std::vector<Track> queue = m_playlist.GetQueueTracks();
                    std::string s(50, '-');
                    std::string res = "[Поиск] Результаты по запросу \"" + qFull.toStdString() + "\":\n" + s + "\n";

                    int matchCount = 0;
                    for (size_t i = 0; i < queue.size(); ++i) {
                        QString trackArtist = QString::fromStdString(queue[i].artist);
                        QString trackTitle = QString::fromStdString(queue[i].title);

                        bool match = false;
                        if (isSplit) {
                            match = trackArtist.contains(qArtist, Qt::CaseInsensitive) &&
                                    trackTitle.contains(qTitle, Qt::CaseInsensitive);
                        } else {
                            match = trackArtist.contains(qFull, Qt::CaseInsensitive) ||
                                    trackTitle.contains(qFull, Qt::CaseInsensitive);
                        }

                        if (match) {
                            res += "[" + std::to_string(i + 1) + "]. " + queue[i].artist + " - " + queue[i].title
                                   + " [" + queue[i].GetFormattedDuration() + "]\n";
                            matchCount++;

                            if (matchCount >= 20) {
                                res += "... Показаны первые 20 совпадений.\n";
                                break;
                            }
                        }
                    }

                    if (matchCount == 0) {
                        res += "Ничего не найдено.\n";
                    }
                    res += s + "\n\n> ";
                    syncPrint(res);
                } else {
                    syncPrint("[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ");
                }
                continue;
            }

            if (lowerInput == "ly" || lowerInput == "lyrics" || lowerInput == "ly new" || lowerInput == "lyrics new") {
                bool isNewFile = (lowerInput.find("new") != std::string::npos);
                Track currentTrack = m_playlist.GetCurrentTrack();

                auto showLyricsFile = [this, currentTrack, isNewFile, syncPrint](const std::string& text) {
                    if (text.empty()) {
                        syncPrint("[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ");
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

                    syncPrint("[Текст] Открыт файл: " + filePath.toStdString() + "\n\n> ");
                };

                std::string text = currentTrack.lyrics;

                if (text.empty()) {
                    syncPrint("[Текст] Поиск текста...\n\n> ");

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
                continue;
            }

            char command = lowerInput[0];
            std::string s(50, '*');
            switch (command) {
                case 'h': {
                    std::string helpText = "\n" + s + "\n"
                              + " [P] Play/Pause\n [N] Next\n [B] Prev\n"
                              + " [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n"
                              + " [st] Standard Order\n [sh] Shuffle (Reshuffles if already on)\n [R] Repeat Mode\n"
                              + " [J <num>] Jump to track\n [cv] Current volume\n"
                              + " [rs] Reset Session (Back to track 1, standard order)\n"
                              + " [mode <0/1>] 0 - Standard, 1 - Gapless transition\n"
                              + " [search <text>] Search tracks in the loaded playlist\n"
                              + " [ly] Show lyrics for current track\n"
                              + " [tl] Export tracklist to TXT\n"
                              + " [dl] / [dl <num>] Download track for offline playback\n"
                              + " [rm] / [rm <num>] Delete downloaded track from local cache\n"
                              + " [Q] Quit\n"
                              + s + "\n\n> ";
                    syncPrint(helpText);
                    break;
                }
                case 'p': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume(); }, Qt::QueuedConnection); break;
                case 'n': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Next(); }, Qt::QueuedConnection); break;
                case 'b': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Previous(); }, Qt::QueuedConnection); break;
                case '+': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                case '-': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                case 'r': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.ToggleRepeat(); }, Qt::QueuedConnection); break;
                case 'i': {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                        Track current = m_playlist.GetCurrentTrack();
                        std::string info = "[Инфо] Артист: " + current.artist + "\n"
                                         + "[Инфо] Название: " + current.title + "\n"
                                         + "[Инфо] ID: " + current.id + "\n"
                                         + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
                        // Для команды i используем прямую очистку
                        std::cout << "\r\033[2K\033[1A\r\033[2K" << info;
                        std::cout.flush();
                    }, Qt::QueuedConnection);
                    break;
                }
                case 'q':
                    emit QuitRequested();
                    m_isRunning = false;
                    return;
            }
        }
    }
}

void ConsoleController::UiLoop() {
    const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    const int numBands = 16; // Количество столбиков

    while (m_isRunning) {
        if (m_currentState == ConsoleState::WAITING_TOKEN_URL) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (m_currentState == ConsoleState::COMMAND_MODE && m_audio.IsPlaying()) {
            double current = m_audio.GetPositionSeconds();
            if (current < 0.0) current = 0.0;
            int currentSecInt = static_cast<int>(current);

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

                int barLength = 40;
                int filled = static_cast<int>((current / total) * barLength);
                if (filled > barLength) filled = barLength;
                if (filled < 0) filled = 0;

                std::string bar = "[";
                for (int i = 0; i < barLength; ++i) {
                    if (i < filled) bar += "\xE2\x96\x88";
                    else bar += "-";
                }
                bar += "]";

                // === ЛОГИКА ВИЗУАЛИЗАТОРА ===
                std::vector<float> fft = m_audio.GetSpectrumData();
                std::string spectrum = " [";

                if (!fft.empty()) {
                    for (int i = 0; i < numBands; ++i) {
                        float peak = 0.0f;
                        // Используем экспоненту, чтобы захватить больше басов и средних частот
                        int startBin = static_cast<int>(std::pow(2.0, i * 7.0 / numBands));
                        int endBin = static_cast<int>(std::pow(2.0, (i + 1) * 7.0 / numBands));
                        if (endBin <= startBin) endBin = startBin + 1;
                        if (endBin > 128) endBin = 128;

                        for (int b = startBin; b < endBin; ++b) {
                            if (fft[b] > peak) peak = fft[b];
                        }

                        int level = static_cast<int>(std::sqrt(peak) * 18.0f);
                        if (level < 0) level = 0;
                        if (level > 7) level = 7;

                        spectrum += blocks[level];
                    }
                } else {
                    spectrum += std::string(numBands, ' ');
                }
                spectrum += "]";

                Track currentTrack = m_playlist.GetCurrentTrack();
                std::vector<Track> queue = m_playlist.GetQueueTracks();
                int trackIndex = 0;

                for (size_t i = 0; i < queue.size(); ++i) {
                    if (queue[i].id == currentTrack.id) {
                        trackIndex = i + 1;
                        break;
                    }
                }

                std::string trackName = std::to_string(trackIndex) + ". " + currentTrack.artist + " - " + currentTrack.title;

                QMetaObject::invokeMethod(QCoreApplication::instance(), [trackName, curMin, curSec, totMin, totSec, bar, percent, spectrum]() {
                    std::lock_guard<std::mutex> lock(Logger::GetMutex());
                    printf("\033[s"
                           "\033[1A"
                           "\r\033[2K%s | %02d:%02d / %02d:%02d %s %d%%%s"
                           "\033[u",
                           trackName.c_str(), curMin, curSec, totMin, totSec, bar.c_str(), percent, spectrum.c_str());
                    fflush(stdout);
                }, Qt::QueuedConnection);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}