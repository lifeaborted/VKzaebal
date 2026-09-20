#include "PlaylistCommands.h"
#include "CommandDispatcher.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <QString>
#include <vector>
#include <string>

namespace {
    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

    std::string Trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    class ExportPlaylistCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
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
                auto trimStr = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t")); s.erase(s.find_last_not_of(" \t") + 1); };
                trimStr(searchArtist);
                trimStr(searchTitle);
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

    class PlaylistControlCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty() || arg == "help") {
                std::string help = "\n[Плейлисты] Команды управления плейлистами:\n"
                                   "  pl <название>       - Создать новый плейлист\n"
                                   "  pl play             - Выбрать плейлист для воспроизведения\n"
                                   "  pl play <название>  - Запустить плейлист с указанным названием\n"
                                   "  pl rm <название>    - Удалить плейлист\n"
                                   "  pls                 - Список всех плейлистов\n"
                                   "  add                 - Добавить текущий трек в плейлист\n"
                                   "  add <номер>         - Добавить трек из очереди в плейлист\n"
                                   "  drop <номер>        - Удалить трек из текущей очереди\n\n> ";
                if (ctx.print) ctx.print(help);
                return;
            }

            if (arg == "play") {
                if (ctx.onSelectPlaylistToPlay) {
                    RunInMainThread([ctx]() { ctx.onSelectPlaylistToPlay(); });
                }
                return;
            }

            if (arg.rfind("play ", 0) == 0) {
                std::string plName = Trim(arg.substr(5));
                if (plName.empty()) {
                    if (ctx.onSelectPlaylistToPlay) {
                        RunInMainThread([ctx]() { ctx.onSelectPlaylistToPlay(); });
                    }
                    return;
                }
                auto pls = ctx.dbManager.GetPlaylists();
                bool found = false;
                for (const auto& p : pls) {
                    if (p.name == plName) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    if (ctx.onSourceChange) {
                        RunInMainThread([ctx, plName]() { ctx.onSourceChange("Custom:" + plName); });
                    }
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Плейлист '" + plName + "' не найден.\n\n> ");
                }
                return;
            }

            if (arg.rfind("rm ", 0) == 0) {
                std::string plName = Trim(arg.substr(3));
                if (plName.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Укажите название плейлиста для удаления. Например: pl rm Мой Плейлист\n\n> ");
                    return;
                }
                bool ok = ctx.dbManager.DeletePlaylist(plName);
                if (ctx.print) {
                    ctx.print(ok ? ("[Плейлисты] Плейлист '" + plName + "' успешно удален.\n\n> ")
                                 : ("[Ошибка] Плейлист '" + plName + "' не найден.\n\n> "));
                }
                return;
            }

            // Создание плейлиста
            bool ok = ctx.dbManager.CreatePlaylist(arg);
            if (ctx.print) {
                ctx.print(ok ? ("[Плейлисты] Плейлист '" + arg + "' успешно создан!\n\n> ")
                             : ("[Ошибка] Плейлист с именем '" + arg + "' уже существует или ошибка создания.\n\n> "));
            }
        }
    };

    class PlaylistListCommand : public IConsoleCommand {
    public:
        void Execute(const std::string&, CommandContext& ctx) override {
            auto playlists = ctx.dbManager.GetPlaylists();
            if (playlists.empty()) {
                if (ctx.print) ctx.print("[Плейлисты] Нет сохраненных плейлистов. Создай через: pl <название>\n\n> ");
                return;
            }

            std::string s(50, '=');
            std::string res = "\n" + s + "\n=== Ваши плейлисты ===\n";
            for (size_t i = 0; i < playlists.size(); ++i) {
                res += " [" + std::to_string(i + 1) + "] " + playlists[i].name
                     + " (" + std::to_string(playlists[i].trackCount) + " треков)\n";
            }
            res += s + "\nИспользуй 'pl play' для запуска или 'pl <название>' для создания нового.\n\n> ";
            if (ctx.print) ctx.print(res);
        }
    };

    class AddTrackToPlaylistCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& arg, CommandContext& ctx) override {
            Track targetTrack;
            if (arg.empty()) {
                targetTrack = ctx.playlist.GetCurrentTrack();
                if (targetTrack.id.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Сейчас никакой трек не играет.\n\n> ");
                    return;
                }
            } else {
                try {
                    int num = std::stoi(arg);
                    int idx = num - 1;
                    std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                    if (idx >= 0 && idx < static_cast<int>(queue.size())) {
                        targetTrack = queue[idx];
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                        return;
                    }
                } catch (...) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: add или add <номер>\n\n> ");
                    return;
                }
            }

            if (!targetTrack.id.empty() && ctx.onSelectPlaylist) {
                RunInMainThread([ctx, targetTrack]() {
                    ctx.onSelectPlaylist(targetTrack);
                });
            }
        }
    };

    class DropTrackFromPlaylistCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Используй: drop <номер трека в очереди>\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                if (idx < 0 || idx >= static_cast<int>(queue.size())) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                    return;
                }

                Track droppedTrack = queue[idx];

                RunInMainThread([ctx, droppedTrack, num]() {
                    auto allTracks = ctx.playlist.GetAllTracks();
                    int absIndex = -1;
                    for (size_t i = 0; i < allTracks.size(); ++i) {
                        if (allTracks[i].id == droppedTrack.id) {
                            absIndex = static_cast<int>(i);
                            break;
                        }
                    }

                    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
                    std::string activeSource = settings.value("General/source", "").toString().toStdString();
                    if (activeSource.rfind("Custom:", 0) == 0 && absIndex >= 0) {
                        std::string plName = activeSource.substr(7);
                        int plId = -1;
                        ctx.dbManager.LoadPlaylistTracksByName(plName, plId);
                        if (plId > 0) {
                            ctx.dbManager.RemoveTrackFromPlaylist(plId, absIndex);
                        }
                    }

                    if (absIndex >= 0) {
                        ctx.playlist.RemoveTrack(absIndex);
                    }

                    if (ctx.print) {
                        ctx.print("[Очередь] Трек #" + std::to_string(num) + " (" + droppedTrack.artist + " - " + droppedTrack.title + ") удален.\n\n> ");
                    }
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: drop <номер трека>\n\n> ");
            }
        }
    };
}

void RegisterPlaylistCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands) {
    commands["tl"] = std::make_unique<ExportPlaylistCommand>();
    commands["search"] = std::make_unique<SearchCommand>();
    commands["pl"] = std::make_unique<PlaylistControlCommand>();
    commands["pls"] = std::make_unique<PlaylistListCommand>();
    commands["add"] = std::make_unique<AddTrackToPlaylistCommand>();
    commands["drop"] = std::make_unique<DropTrackFromPlaylistCommand>();
}
