#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "core/shazam/ShazamFFI.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <cmath>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QtConcurrent>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace {
    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

    // КЛАССЫ КОМАНД
    class PlayPauseCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                if (ctx.audio.IsPlaying()) ctx.audio.Pause(); else ctx.audio.Resume();
            });
        }
    };

    class NextCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.Next(); });
        }
    };

    class PrevCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.Previous(); });
        }
    };

    class VolumeAdjustCommand : public IConsoleCommand {
        float m_delta;
    public:
        explicit VolumeAdjustCommand(float delta) : m_delta(delta) {}
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx, delta = m_delta]() { ctx.audio.SetVolume(ctx.audio.GetVolume() + delta); });
        }
    };

    class VolumeSetCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                int vol = std::stoi(arg);
                if (vol < 0) vol = 0;
                if (vol > 100) vol = 100;
                RunInMainThread([ctx, vol]() { ctx.audio.SetVolume(vol / 100.0f); });
                if (ctx.print) ctx.print("[Громкость] Установлена громкость: " + std::to_string(vol) + "%\n\n> ");
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ");
            }
        }
    };

    class VolumeCurrentCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            int vol = static_cast<int>(std::round(ctx.audio.GetVolume() * 100));
            if (ctx.print) ctx.print("[Громкость] Текущая громкость: " + std::to_string(vol) + "%\n\n> ");
        }
    };

    class SeekCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                double pos = std::stod(arg);
                RunInMainThread([ctx, pos]() { ctx.audio.SetPositionSeconds(pos); });
                if (ctx.print) ctx.print("[Перемотка] Переход на " + std::to_string(static_cast<int>(pos)) + " сек.\n\n> ");
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: seek <секунды>\n\n> ");
            }
        }
    };

    class RepeatCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.ToggleRepeat(); });
        }
    };

    class JumpCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                int idx = std::stoi(arg);
                RunInMainThread([ctx, idx]() { ctx.playlist.JumpToQueueIndex(idx - 1); });
                if (ctx.print) ctx.print("[Плейлист] Переход к треку " + std::to_string(idx) + "\n\n> ");
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
            }
        }
    };

    class ExportPlaylistCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
        }
    };

    class ShuffleCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(true);
                ctx.playlist.JumpToQueueIndex(0);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Плейлист] Режим: Перемешивание (Shuffle). Стартуем случайный трек!\n\n> ");
        }
    };

    class StandardOrderCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(false);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Плейлист] Режим: Стандартный порядок\n\n> ");
        }
    };

    class ResetSessionCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(false);
                ctx.playlist.JumpTo(0);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
        }
    };

    class SearchCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ");
                return;
            }

            std::string searchArtist = arg;
            std::string searchTitle = arg;
            bool isSplit = false;

            size_t dashPos = arg.find("-");
            if (dashPos != std::string::npos) {
                searchArtist = arg.substr(0, dashPos);
                searchTitle = arg.substr(dashPos + 1);
                auto trim = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t")); s.erase(s.find_last_not_of(" \t") + 1); };
                trim(searchArtist);
                trim(searchTitle);
                isSplit = true;
            }

            QString qArtist = QString::fromStdString(searchArtist).trimmed();
            QString qTitle = QString::fromStdString(searchTitle).trimmed();
            QString qFull = QString::fromStdString(arg).trimmed();

            std::vector<Track> queue = ctx.playlist.GetQueueTracks();
            std::string s(50, '-');
            std::string res = "[Поиск] Результаты по запросу \"" + qFull.toStdString() + "\":\n" + s + "\n";

            int matchCount = 0;
            for (size_t i = 0; i < queue.size(); ++i) {
                QString trackArtist = QString::fromStdString(queue[i].artist);
                QString trackTitle = QString::fromStdString(queue[i].title);
                bool match = isSplit ? (trackArtist.contains(qArtist, Qt::CaseInsensitive) && trackTitle.contains(qTitle, Qt::CaseInsensitive))
                                     : (trackArtist.contains(qFull, Qt::CaseInsensitive) || trackTitle.contains(qFull, Qt::CaseInsensitive));
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
            if (ctx.print) ctx.print(res);
        }
    };

    class DownloadControlCommand : public IConsoleCommand {
        bool m_isRemove;
    public:
        explicit DownloadControlCommand(bool isRemove) : m_isRemove(isRemove) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            Track targetTrack;
            bool isValid = false;

            if (arg.empty()) {
                targetTrack = ctx.playlist.GetCurrentTrack();
                isValid = true;
            } else {
                try {
                    int idx = std::stoi(arg) - 1;
                    std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                    if (idx >= 0 && idx < queue.size()) {
                        targetTrack = queue[idx];
                        isValid = true;
                    }
                } catch(...) {}
            }

            if (isValid && !targetTrack.id.empty()) {
                QString pathMp3 = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "mp3");
                QString pathAac = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "aac");

                if (!m_isRemove) {
                    if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                        if (ctx.print) ctx.print("[Загрузка] Трек уже скачан.\n\n> ");
                    } else {
                        if (!ctx.currentProvider) {
                            if (ctx.print) ctx.print("[Ошибка] Нет активного онлайн-источника для скачивания.\n\n> ");
                            return;
                        }
                        if (ctx.print) ctx.print("[Загрузка] Получение ссылки для " + targetTrack.title + "...\n\n> ");

                        RunInMainThread([ctx, targetTrack]() {
                            ctx.currentProvider->FetchTrackUrl(targetTrack.id, [ctx, targetTrack](const std::string& url, bool err) {
                                if (!err && !url.empty()) ctx.downloader.Download(targetTrack, url);
                                else Logger::Log(LogLevel::WARNING, "Failed to get URL for download.");
                            });
                        });
                    }
                } else {
                    if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                        QFile::remove(pathMp3);
                        QFile::remove(pathAac);
                        if (ctx.print) ctx.print("[Кэш] Удален: " + targetTrack.artist + " - " + targetTrack.title + "\n\n> ");
                    } else {
                        if (ctx.print) ctx.print("[Кэш] Трек не был скачан.\n\n> ");
                    }
                }
            } else {
                if (ctx.print) ctx.print("[Ошибка] Не удалось найти трек.\n\n> ");
            }
        }
    };

    class LyricsCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            bool isNewFile = (arg.find("new") != std::string::npos);
            Track currentTrack = ctx.playlist.GetCurrentTrack();

            auto showLyricsFile = [ctx, currentTrack, isNewFile](const std::string& text) {
                if (text.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ");
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
                if (ctx.print) ctx.print("[Текст] Открыт файл: " + filePath.toStdString() + "\n\n> ");
            };

            std::string text = currentTrack.lyrics;
            if (text.empty()) {
                if (ctx.print) ctx.print("[Текст] Поиск текста...\n\n> ");
                RunInMainThread([ctx, currentTrack, showLyricsFile]() {
                    ctx.lyricsFetcher.FetchLyrics(currentTrack.artist, currentTrack.title, [ctx, currentTrack, showLyricsFile](const std::string& fetchedText) {
                        if (!fetchedText.empty()) ctx.dbManager.UpdateTrackLyrics(currentTrack.id, fetchedText);
                        showLyricsFile(fetchedText);
                    });
                });
            } else {
                showLyricsFile(text);
            }
        }
    };

    class ShazamCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Shazam] Использование: shazam <путь_к_файлу>\n\n> ");
                return;
            }
            if (ctx.print) ctx.print("[Shazam] Анализ трека через нативное Rust-ядро...\n> ");

            QThreadPool::globalInstance()->start([ctx, argStr = arg]() {
                char* raw_base64 = generate_shazam_signature(argStr.c_str());
                if (!raw_base64) {
                    if (ctx.print) ctx.print("\n[Shazam] Ошибка генерации подписи в Rust-ядре.\n> ");
                    return;
                }
                QString base64Sig = QString::fromUtf8(raw_base64);
                free_shazam_string(raw_base64);

                RunInMainThread([ctx, base64Sig]() {
                    QJsonObject sigObj{ {"uri", base64Sig}, {"samplems", 12000} };
                    QJsonObject rootObj{ {"signature", sigObj} };
                    QByteArray jsonPayload = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

                    QNetworkAccessManager* manager = new QNetworkAccessManager();
                    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    QUrl url("https://amp.shazam.com/discovery/v5/ru/RU/android/-/tag/" + uuid + "/" + uuid);

                    QNetworkRequest request(url);
                    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                    request.setRawHeader("User-Agent", "Shazam Android/13.7.0");

                    QNetworkReply* reply = manager->post(request, jsonPayload);

                    QObject::connect(reply, &QNetworkReply::finished, [ctx, reply, manager]() {
                        if (reply->error() == QNetworkReply::NoError) {
                            QJsonObject trackObj = QJsonDocument::fromJson(reply->readAll()).object()["track"].toObject();
                            if (trackObj.isEmpty()) {
                                if (ctx.print) ctx.print("\n[Shazam] Трек не распознан :( Возможно, его нет в базе.\n> ");
                            } else {
                                QString title = trackObj["title"].toString();
                                QString artist = trackObj["subtitle"].toString();
                                if (ctx.print) ctx.print("\n[Shazam] 🎵 УСПЕХ! Найдено: " + artist.toStdString() + " - " + title.toStdString() + "\n> ");
                            }
                        } else {
                            if (ctx.print) ctx.print("\n[Shazam] Ошибка сети: " + reply->errorString().toStdString() + "\n> ");
                        }
                        reply->deleteLater();
                        manager->deleteLater();
                    });
                });
            });
        }
    };

    class ConfigCommand : public IConsoleCommand {
        std::string m_cmdType;
    public:
        explicit ConfigCommand(const std::string& type) : m_cmdType(type) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (m_cmdType == "source") {
                if (ctx.print) ctx.print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - SoundCloud\n4 - Yandex\n5 - Оффлайн режим\n\nВведите номер: ");
                if (ctx.onSourceChange) ctx.onSourceChange("SELECT");
            } else if (m_cmdType == "vis") {
                if (ctx.onVisualizerToggle) ctx.onVisualizerToggle();
            } else if (m_cmdType == "mode") {
                try {
                    int mode = std::stoi(arg);
                    if (mode == 0 || mode == 1) {
                        bool isGapless = (mode == 1);
                        if (ctx.onGaplessMode) RunInMainThread([ctx, isGapless]() { ctx.onGaplessMode(isGapless); });
                        if (ctx.print) ctx.print("[Режим] Установлен " + std::string(isGapless ? "плавный (gapless)" : "стандартный") + " переход.\n\n> ");
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
                    }
                } catch (...) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
                }
            } else if (m_cmdType == "reload") {
                if (ctx.onReloadUi) RunInMainThread([ctx]() { ctx.onReloadUi(); });
            }
        }
    };

    class SystemCommand : public IConsoleCommand {
        std::string m_cmdType;
    public:
        explicit SystemCommand(const std::string& type) : m_cmdType(type) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (m_cmdType == "logout") {
                if (arg == "vk" || arg == "spotify" || arg == "sc" || arg == "yandex" || arg == "all") {
                    if (ctx.onLogout) RunInMainThread([ctx, arg]() { ctx.onLogout(arg); });
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout all\n\n> ");
                }
            } else if (m_cmdType == "info") {
                RunInMainThread([ctx]() {
                    Track current = ctx.playlist.GetCurrentTrack();
                    std::string info = "[Инфо] Артист: " + current.artist + "\n"
                                     + "[Инфо] Название: " + current.title + "\n"
                                     + "[Инфо] ID: " + current.id + "\n"
                                     + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
                    if (ctx.print) ctx.print(info);
                });
            } else if (m_cmdType == "quit") {
                if (ctx.onQuit) ctx.onQuit();
            } else if (m_cmdType == "help") {
                std::string s(50, '*');
                std::string helpText = "\n" + s + "\n [P] Play/Pause\n [N] Next\n [B] Prev\n [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n [st] Standard Order\n [sh] Shuffle\n [R] Repeat Mode\n [J <num>] Jump to track\n [cv] Current volume\n [rs] Reset Session\n [mode <0/1>] 0 - Standard, 1 - Gapless transition\n [search <text>] Search tracks in playlist\n [ly] Show lyrics for current track\n [logout <service>] Logout from choosen service\n [source] Select audio source\n [tl] Export tracklist to TXT\n [dl] / [dl <num>] Download track\n [rm] / [rm <num>] Delete downloaded track\n [vis] Toggle visualizer\n [Q] Quit\n" + s + "\n\n> ";
                if (ctx.print) ctx.print(helpText);
            }
        }
    };
}

CommandDispatcher::CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist, DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher)
    : m_audio(audio), m_playlist(playlist), m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher) {
    RegisterCommands();
}

CommandDispatcher::~CommandDispatcher() = default;

void CommandDispatcher::SetCurrentProvider(IAudioProvider* provider) { m_currentProvider = provider; }
void CommandDispatcher::SetPrintCallback(std::function<void(const std::string&)> printCb) { m_printCb = printCb; }
void CommandDispatcher::Print(const std::string& msg) { if (m_printCb) m_printCb(msg); }

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
        CommandContext ctx {
            m_audio, m_playlist, m_dbManager, m_downloader, m_lyricsFetcher, m_currentProvider,
            m_printCb, OnSourceChangeRequested, OnGaplessModeChanged, OnVisualizerToggled,
            OnQuitRequested, OnLogoutRequested, OnReloadUiRequested
        };
        it->second->Execute(arg, ctx);
    } else {
        Print("[Ошибка] Неизвестная команда. Введи 'h' для справки.\n\n> ");
    }
}

class SavePosCommand : public IConsoleCommand {
    void Execute(const std::string& arg, CommandContext& ctx) override {
        try {
            int mode = std::stoi(arg);
            if (mode == 0 || mode == 1) {
                bool savePos = (mode == 1);
                QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Playback/SavePosition", savePos);
                std::string msg = savePos ? "[Режим] Теперь плеер запоминает позицию в треке при выходе.\n\n> "
                                          : "[Режим] Теперь плеер запоминает только трек.\n\n> ";
                if (ctx.print) ctx.print(msg);
            } else {
                if (ctx.print) ctx.print("[Ошибка] Используй: savepos 0 (только трек) или savepos 1 (трек + время)\n\n> ");
            }
        } catch (...) {
            if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: savepos 0 или savepos 1\n\n> ");
        }
    }
};

void CommandDispatcher::RegisterCommands() {
    m_commands["p"] = std::make_unique<PlayPauseCommand>();
    m_commands["n"] = std::make_unique<NextCommand>();
    m_commands["b"] = std::make_unique<PrevCommand>();
    m_commands["+"] = std::make_unique<VolumeAdjustCommand>(0.1f);
    m_commands["-"] = std::make_unique<VolumeAdjustCommand>(-0.1f);
    m_commands["v"] = std::make_unique<VolumeSetCommand>();
    m_commands["cv"] = std::make_unique<VolumeCurrentCommand>();
    m_commands["seek"] = std::make_unique<SeekCommand>();
    m_commands["r"] = std::make_unique<RepeatCommand>();
    m_commands["j"] = std::make_unique<JumpCommand>();
    m_commands["tl"] = std::make_unique<ExportPlaylistCommand>();
    m_commands["sh"] = std::make_unique<ShuffleCommand>();
    m_commands["st"] = std::make_unique<StandardOrderCommand>();
    m_commands["rs"] = std::make_unique<ResetSessionCommand>();
    m_commands["reset"] = std::make_unique<ResetSessionCommand>();
    m_commands["search"] = std::make_unique<SearchCommand>();
    m_commands["dl"] = std::make_unique<DownloadControlCommand>(false);
    m_commands["rm"] = std::make_unique<DownloadControlCommand>(true);
    m_commands["ly"] = std::make_unique<LyricsCommand>();
    m_commands["lyrics"] = std::make_unique<LyricsCommand>();
    m_commands["shazam"] = std::make_unique<ShazamCommand>();

    m_commands["source"] = std::make_unique<ConfigCommand>("source");
    m_commands["vis"] = std::make_unique<ConfigCommand>("vis");
    m_commands["mode"] = std::make_unique<ConfigCommand>("mode");
    m_commands["reload"] = std::make_unique<ConfigCommand>("reload");
    m_commands["savepos"] = std::make_unique<SavePosCommand>();

    m_commands["logout"] = std::make_unique<SystemCommand>("logout");
    m_commands["i"] = std::make_unique<SystemCommand>("info");
    m_commands["q"] = std::make_unique<SystemCommand>("quit");
    m_commands["h"] = std::make_unique<SystemCommand>("help");
}