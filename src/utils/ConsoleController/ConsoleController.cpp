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

#include "ConsoleController.h"
#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/DatabaseManager/DatabaseManager.h"
#include "utils/logger/logger.h"


ConsoleController::ConsoleController(
    AudioEngine& audio,
    PlaylistManager& playlist,
    VkAuthManager& authManager,
    DatabaseManager& dbManager,
    VkApiClient& vkClient,
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
    m_isRunning = true;
    m_inputThread = std::thread(&ConsoleController::InputLoop, this);
    m_uiThread = std::thread(&ConsoleController::UiLoop, this);
}

void ConsoleController::Stop() {
    m_isRunning = false;
    if (m_inputThread.joinable()) m_inputThread.detach();
    if (m_uiThread.joinable()) m_uiThread.join();
}

void ConsoleController::InputLoop() {
    std::string rawInput;
    std::string clearLine = "\r\033[2K";

    while (m_isRunning) {
        std::getline(std::cin, rawInput);

        // Пользователь нажал Enter, терминал съехал на 1 строку.
        // Идем наверх, стираем старый прогресс бар, спускаемся обратно
        std::cout << "\033[2A\r\033[2K\033[2B";
        std::cout.flush();

        size_t start = rawInput.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            std::cout << clearLine << "\n> ";
            std::cout.flush();
            continue;
        }
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
                    QString path = "downloads/" + QString::fromStdString(targetTrack.id) + ".wav";

                    if (cmdType == "dl") {
                        if (QFile::exists(path)) {
                            std::cout << clearLine << "[Загрузка] Трек уже скачан.\n\n> ";
                            std::cout.flush();
                        } else {
                            std::cout << clearLine << "[Загрузка] Получение ссылки для " << targetTrack.title << "...\n\n> ";
                            std::cout.flush();

                            QMetaObject::invokeMethod(QCoreApplication::instance(), [&, targetTrack]() {
                                m_vkClient.FetchTrackUrl(targetTrack.id, [this, targetTrack](const std::string& url, bool err) {
                                    if (!err && !url.empty()) {
                                        m_downloader.Download(targetTrack, url);
                                    } else {
                                        Logger::Log(LogLevel::ERROR, "Failed to get URL for download.");
                                    }
                                });
                            }, Qt::QueuedConnection);
                        }
                    } else if (cmdType == "rm") {
                        if (QFile::exists(path) || QFile::exists(path)) {
                            QFile::remove(path);
                            QFile::remove(path);
                            std::cout << clearLine << "[Кэш] Удален: " << targetTrack.artist << " - " << targetTrack.title << "\n\n> ";
                            std::cout.flush();
                        } else {
                            std::cout << clearLine << "[Кэш] Трек не был скачан.\n\n> ";
                            std::cout.flush();
                        }
                    }
                }
                continue;
            }

            // --- КОМАНДА: Установка громкости (v <num>) ---
            if (lowerInput.length() >= 2 && lowerInput[0] == 'v' && lowerInput[1] == ' ') {
                try {
                    int vol = std::stoi(input.substr(2));
                    if (vol < 0) vol = 0;
                    if (vol > 100) vol = 100;

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&, vol]() {
                        m_audio.SetVolume(vol / 100.0f);
                    }, Qt::QueuedConnection);

                    std::cout << clearLine << "[Громкость] Установлена громкость: " << vol << "%\n\n> ";
                    std::cout.flush();
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- КОМАНДА: Текущая громкость ---
            if (lowerInput == "cv") {
                int vol = static_cast<int>(m_audio.GetVolume() * 100);
                std::cout << clearLine << "[Громкость] Текущая громкость: " << vol << "%\n\n> ";
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
                    std::cout << clearLine << "[Плейлист] Переход к треку " << idx << "\n\n> ";
                    std::cout.flush();
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный номер трека.\n\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- КОМАНДА: Экспорт плейлиста (tl) ---
            if (lowerInput == "tl") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                std::cout << clearLine << "[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ";
                std::cout.flush();
                continue;
            }

            // --- КОМАНДА: Перемешивание (sh) ---
            if (lowerInput == "sh") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(true);
                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                std::cout << clearLine << "[Плейлист] Режим: Перемешивание (Shuffle)\n\n> ";
                std::cout.flush();
                continue;
            }

            // --- КОМАНДА: Стандартный порядок (st) ---
            if (lowerInput == "st") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(false);
                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                std::cout << clearLine << "[Плейлист] Режим: Стандартный порядок\n\n> ";
                std::cout.flush();
                continue;
            }

            // --- КОМАНДА: Сброс сессии (rs / reset) ---
            if (lowerInput == "rs" || lowerInput == "reset") {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                    m_playlist.SetShuffle(false);
                    m_playlist.JumpTo(0);
                    m_dbManager.SaveQueue(m_playlist.GetQueueTracks(), m_playlist.IsShuffle());
                    m_dbManager.ExportQueueToTxt("playlist.txt", m_playlist.IsShuffle());
                }, Qt::QueuedConnection);

                std::cout << clearLine << "[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ";
                std::cout.flush();
                continue;
            }

            // --- Смена режима перехода (mode <0/1>) ---
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

                        std::cout << clearLine << "[Режим] Установлен " << (isGapless ? "плавный (gapless)" : "стандартный") << " переход.\n\n> ";
                        std::cout.flush();
                    } else {
                        std::cout << clearLine << "[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ";
                        std::cout.flush();
                    }
                } catch (...) {
                    std::cout << clearLine << "[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- Поиск по плейлисту (search <query>) ---
            if (lowerInput.length() > 7 && lowerInput.substr(0, 7) == "search ") {
                QString query = QString::fromStdString(input.substr(7)).trimmed();

                if (!query.isEmpty()) {
                    std::vector<Track> queue = m_playlist.GetQueueTracks();
                    std::cout << clearLine << "[Поиск] Результаты по запросу \"" << query.toStdString() << "\":\n";
                    std::string s(50, '-');
                    std::cout << s << "\n";

                    int matchCount = 0;
                    for (size_t i = 0; i < queue.size(); ++i) {
                        QString artist = QString::fromStdString(queue[i].artist);
                        QString title = QString::fromStdString(queue[i].title);

                        if (artist.contains(query, Qt::CaseInsensitive) || title.contains(query, Qt::CaseInsensitive)) {
                            std::cout << "[" << (i + 1) << "]. " << queue[i].artist << " - " << queue[i].title
                                      << " [" << queue[i].GetFormattedDuration() << "]\n";
                            matchCount++;

                            if (matchCount >= 20) {
                                std::cout << "... Показаны первые 20 совпадений.\n";
                                break;
                            }
                        }
                    }

                    if (matchCount == 0) {
                        std::cout << "Ничего не найдено.\n";
                    }
                    std::cout << s << "\n\n> ";
                    std::cout.flush();
                } else {
                    std::cout << clearLine << "[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ";
                    std::cout.flush();
                }
                continue;
            }

            // --- КОМАНДА: Текст песни (ly / ly new) ---
            if (lowerInput == "ly" || lowerInput == "lyrics" || lowerInput == "ly new" || lowerInput == "lyrics new") {
                bool isNewFile = (lowerInput.find("new") != std::string::npos);
                Track currentTrack = m_playlist.GetCurrentTrack();

                auto showLyricsFile = [this, currentTrack, isNewFile, clearLine](const std::string& text) {
                    std::cout << "\r\033[2K\033[1A\033[2K\r"; // Зачищаем асинхронные хвосты от Logger
                    if (text.empty()) {
                        std::cout << "[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ";
                        std::cout.flush();
                        return;
                    }

                    QDir().mkpath("lyrics");
                    QString filePath;

                    if (isNewFile) {
                        QString safeArtist = QString::fromStdString(currentTrack.artist).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                        QString safeTitle = QString::fromStdString(currentTrack.title).replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
                        filePath = "lyrics/" + safeArtist + " - " + safeTitle + ".txt";
                    } else {
                        filePath = "lyrics/lyric.txt";
                    }

                    QFile file(filePath);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        file.write(QByteArray::fromStdString(text));
                        file.close();
                    }

                    QFileInfo fileInfo(filePath);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));

                    std::cout << "[Текст] Открыт файл: " << filePath.toStdString() << "\n\n> ";
                    std::cout.flush();
                };

                std::string text = currentTrack.lyrics;

                if (text.empty()) {
                    std::cout << clearLine << "[Текст] Поиск текста...\n\n> ";
                    std::cout.flush();

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
                              << " [st] Standard Order\n [sh] Shuffle (Reshuffles if already on)\n [R] Repeat Mode\n"
                              << " [J <num>] Jump to track\n [cv] Current volume\n"
                              << " [rs] Reset Session (Back to track 1, standard order)\n"
                              << " [mode <0/1>] 0 - Standard, 1 - Gapless transition\n"
                              << " [search <text>] Search tracks in the loaded playlist\n"
                              << " [ly] Show lyrics for current track\n"
                              << " [tl] Export tracklist to TXT\n"
                              << " [dl] / [dl <num>] Download track for offline playback\n"
                              << " [rm] / [rm <num>] Delete downloaded track from local cache\n"
                              << " [Q] Quit\n"
                              <<s
                              <<"\n\n> ";
                    std::cout.flush();
                    break;
                case 'p': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { if (m_audio.IsPlaying()) m_audio.Pause(); else m_audio.Resume(); }, Qt::QueuedConnection); break;
                case 'n': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Next(); }, Qt::QueuedConnection); break;
                case 'b': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.Previous(); }, Qt::QueuedConnection); break;
                case '+': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() + 0.1f); }, Qt::QueuedConnection); break;
                case '-': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_audio.SetVolume(m_audio.GetVolume() - 0.1f); }, Qt::QueuedConnection); break;
                case 'r': QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { m_playlist.ToggleRepeat(); }, Qt::QueuedConnection); break;
                case 'i':
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                        Track current = m_playlist.GetCurrentTrack();
                        std::cout << clearLine << "[Инфо] Артист: " << current.artist << "\n"
                                  << "[Инфо] Название: " << current.title << "\n"
                                  << "[Инфо] ID: " << current.id << "\n"
                                  << "[Инфо] Обложка: " << (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) << "\n\n> ";
                        std::cout.flush();
                    }, Qt::QueuedConnection);
                    break;
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

                    // \033[s   - Сохранить позицию курсора
                    // \033[1A  - Подняться на 1 строку вверх (над строкой ввода "> ")
                    // \r\033[2K - Очистить строку
                    // \033[u   - Вернуть курсор обратно к вводу
                    printf("\033[s"
                           "\033[1A"
                           "\r\033[2K%s | %02d:%02d / %02d:%02d %s %d%%"
                           "\033[u",
                           trackName.c_str(), curMin, curSec, totMin, totSec, bar.c_str(), percent);
                    fflush(stdout);
                }
            }
        } else {
            lastSecond = -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}