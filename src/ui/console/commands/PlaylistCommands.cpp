#include "PlaylistCommands.h"
#include "CommandDispatcher.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "utils/path/PathManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/api/IAudioProvider.h"
#include "services/downloader/TrackDownloader.h"
#include "utils/platform/IDialogService.h"
#include "utils/logger/Logger.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <QString>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <cctype>

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
            RunInMainThread([pl = &ctx.playlist, db = &ctx.dbManager]() {
                db->ExportQueueToTxt(pl->GetQueueTracks(), "playlist.txt", pl->IsShuffle());
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
            if (arg.empty()) {
                std::string help = "\n=== Управление плейлистами ===\n"
                                   "pl <название>       - Создать новый плейлист\n"
                                   "pl play             - Выбрать и включить плейлист (интерактивно)\n"
                                   "pl play <название>  - Включить плейлист по названию\n"
                                   "pl rm <название>    - Удалить плейлист\n"
                                   "pls                 - Показать все плейлисты\n"
                                   "add                 - Добавить текущий трек в плейлист\n"
                                   "add <номер>         - Добавить трек из очереди по номеру в плейлист\n"
                                   "drop <номер>        - Удалить трек из текущей очереди\n\n> ";
                if (ctx.print) ctx.print(help);
                return;
            }

            if (arg == "play") {
                if (ctx.onSelectPlaylistToPlay) {
                    RunInMainThread([cb = ctx.onSelectPlaylistToPlay]() { cb(); });
                }
                return;
            }

            if (arg.rfind("play ", 0) == 0) {
                std::string plName = Trim(arg.substr(5));
                if (plName.empty()) {
                    if (ctx.onSelectPlaylistToPlay) {
                        RunInMainThread([cb = ctx.onSelectPlaylistToPlay]() { cb(); });
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
                        RunInMainThread([cb = ctx.onSourceChange, plName = std::move(plName)]() { cb("Custom:" + plName); });
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
                RunInMainThread([cb = ctx.onSelectPlaylist, targetTrack = std::move(targetTrack)]() {
                    cb(targetTrack);
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

                RunInMainThread([pl = &ctx.playlist, db = &ctx.dbManager, print = ctx.print, droppedTrack = std::move(droppedTrack), num]() {
                    auto allTracks = pl->GetAllTracks();
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
                        db->LoadPlaylistTracksByName(plName, plId);
                        if (plId > 0) {
                            db->RemoveTrackFromPlaylist(plId, absIndex);
                        }
                    }

                    if (absIndex >= 0) {
                        pl->RemoveTrack(absIndex);
                    }

                    if (print) {
                        print("[Очередь] Трек #" + std::to_string(num) + " (" + droppedTrack.artist + " - " + droppedTrack.title + ") удален.\n\n> ");
                    }
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: drop <номер трека>\n\n> ");
            }
        }
    };

    std::mutex s_searchMutex;
    std::vector<Track> s_lastSearchResults;

    class FindCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty()) {
                if (ctx.print) {
                    std::string help = "\n=== Онлайн поиск треков ===\n"
                                       "Команды:\n"
                                       "  find <запрос>        - Поиск по всем сервисам (VK, Ya, YT, SC)\n"
                                       "  find vk <запрос>     - Поиск в ВКонтакте\n"
                                       "  find ya <запрос>     - Поиск в Яндекс Музыке\n"
                                       "  find yt <запрос>     - Поиск в YouTube Music\n"
                                       "  find sc <запрос>     - Поиск в SoundCloud\n"
                                       "  find all <запрос>    - Поиск во всех сервисах\n\n"
                                       "Действия с найденными треками:\n"
                                       "  pf <номер>           - Воспроизвести найденный трек (playfind)\n"
                                       "  lf <номер>           - Добавить в избранное сервиса (likefind)\n"
                                       "  af <номер>           - Добавить в локальный плейлист (addfind)\n"
                                       "  df <номер>           - Скачать трек (dlfind)\n\n> ";
                    ctx.print(help);
                }
                return;
            }

            if (!ctx.router) {
                if (ctx.print) ctx.print("[Ошибка] Роутер источников недоступен.\n\n> ");
                return;
            }

            std::string targetSource = "all";
            std::string query = arg;

            size_t firstSpace = arg.find(' ');
            if (firstSpace != std::string::npos) {
                std::string prefix = arg.substr(0, firstSpace);
                for (char& c : prefix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (prefix == "vk" || prefix == "ya" || prefix == "yandex" ||
                    prefix == "yt" || prefix == "youtube" ||
                    prefix == "sc" || prefix == "soundcloud" ||
                    prefix == "all") {
                    if (prefix == "ya" || prefix == "yandex") targetSource = "Yandex";
                    else if (prefix == "yt" || prefix == "youtube") targetSource = "YouTube";
                    else if (prefix == "sc" || prefix == "soundcloud") targetSource = "SoundCloud";
                    else if (prefix == "vk") targetSource = "VK";
                    else targetSource = "all";

                    query = Trim(arg.substr(firstSpace + 1));
                }
            }

            if (query.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Пустой поисковый запрос.\n\n> ");
                return;
            }

            if (ctx.print) {
                ctx.print("[Поиск] Запрос \"" + query + "\" [" + targetSource + "]... Поиск в сервисах...\n\n> ");
            }

            RunInMainThread([router = ctx.router, targetSource, query, print = ctx.print]() {
                router->Search(targetSource, query, 20, 0, [print, query, targetSource](const std::vector<Track>& tracks, const std::string& err) {
                    if (!err.empty()) {
                        if (print) print("[Ошибка] Ошибка поиска: " + err + "\n\n> ");
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(s_searchMutex);
                        s_lastSearchResults = tracks;
                    }

                    if (tracks.empty()) {
                        if (print) print("[Поиск] По запросу \"" + query + "\" в [" + targetSource + "] ничего не найдено.\n\n> ");
                        return;
                    }

                    std::string s(55, '-');
                    std::string out = "\n" + s + "\n[Поиск] Результаты по запросу \"" + query + "\" (" + std::to_string(tracks.size()) + " треков):\n" + s + "\n";
                    for (size_t i = 0; i < tracks.size(); ++i) {
                        const auto& t = tracks[i];
                        std::string srcTag = t.source.empty() ? "" : ("[" + t.source + "] ");
                        std::string dur = t.GetFormattedDuration();
                        out += "[" + std::to_string(i + 1) + "] " + srcTag + t.artist + " - " + t.title;
                        if (!dur.empty() && dur != "0:00") {
                            out += " (" + dur + ")";
                        }
                        out += "\n";
                    }
                    out += s + "\n"
                           "  pf <номер> - Воспроизвести  |  lf <номер> - В избранное сервиса\n"
                           "  af <номер> - В плейлист     |  df <номер> - Скачать\n" + s + "\n\n> ";

                    if (print) print(out);
                });
            });
        }
    };

    class PlayFindCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Укажите номер трека: pf <номер>\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                Track track;
                {
                    std::lock_guard<std::mutex> lock(s_searchMutex);
                    if (idx < 0 || idx >= static_cast<int>(s_lastSearchResults.size())) {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека (в результатах всего " + std::to_string(s_lastSearchResults.size()) + ").\n\n> ");
                        return;
                    }
                    track = s_lastSearchResults[idx];
                }

                RunInMainThread([pl = &ctx.playlist, track, print = ctx.print]() {
                    pl->AddTrack(track);
                    int lastIdx = static_cast<int>(pl->GetQueueTracks().size()) - 1;
                    pl->JumpToQueueIndex(lastIdx);
                    if (print) {
                        print("[Воспроизведение] Запущен найденный трек: " + track.artist + " - " + track.title + " [" + track.source + "]\n\n> ");
                    }
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат номера: pf <номер>\n\n> ");
            }
        }
    };

    class LikeFindCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Укажите номер трека: lf <номер>\n\n> ");
                return;
            }

            if (!ctx.router) {
                if (ctx.print) ctx.print("[Ошибка] Роутер недоступен.\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                Track track;
                {
                    std::lock_guard<std::mutex> lock(s_searchMutex);
                    if (idx < 0 || idx >= static_cast<int>(s_lastSearchResults.size())) {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                        return;
                    }
                    track = s_lastSearchResults[idx];
                }

                std::string title = track.artist + " - " + track.title;
                std::string src = track.source;

                RunInMainThread([router = ctx.router, track, title, src, print = ctx.print]() {
                    router->AddTrackToFavorites(track, [print, title, src](bool success, const std::string& err) {
                        if (success) {
                            if (print) print("[Избранное] Трек \"" + title + "\" добавлен в избранное [" + src + "]!\n\n> ");
                        } else {
                            if (print) print("[Ошибка] Не удалось добавить в избранное [" + src + "]: " + err + "\n\n> ");
                        }
                    });
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат номера: lf <номер>\n\n> ");
            }
        }
    };

    class AddFindCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Укажите номер трека: af <номер>\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                Track track;
                {
                    std::lock_guard<std::mutex> lock(s_searchMutex);
                    if (idx < 0 || idx >= static_cast<int>(s_lastSearchResults.size())) {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                        return;
                    }
                    track = s_lastSearchResults[idx];
                }

                if (ctx.onSelectPlaylist) {
                    RunInMainThread([cb = ctx.onSelectPlaylist, track]() {
                        cb(track);
                    });
                }
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат номера: af <номер>\n\n> ");
            }
        }
    };

    class DlFindCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Укажите номер трека: df <номер>\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                Track track;
                {
                    std::lock_guard<std::mutex> lock(s_searchMutex);
                    if (idx < 0 || idx >= static_cast<int>(s_lastSearchResults.size())) {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                        return;
                    }
                    track = s_lastSearchResults[idx];
                }

                if (!ctx.router) return;
                IAudioProvider* prov = ctx.router->GetOrCreateProvider(track.source);
                if (!prov) {
                    if (ctx.print) ctx.print("[Ошибка] Не найден провайдер для источника " + track.source + "\n\n> ");
                    return;
                }

                QString customDir = PathManager::GetCustomDownloadsDir();
                QString targetDir = customDir;
                if (targetDir.isEmpty()) {
                    std::string nativeFolder = ctx.dialogService.ChooseFolderDialog("Выберите папку для сохранения аудио");
                    targetDir = QString::fromStdString(nativeFolder);
                    if (targetDir.isEmpty()) {
                        if (ctx.print) ctx.print("[Загрузка] Скачивание отменено (папка не выбрана).\n\n> ");
                        return;
                    }
                    PathManager::SetSessionDownloadsDir(targetDir);
                }

                if (ctx.print) ctx.print("[Загрузка] Получение ссылки для " + track.title + "...\n\n> ");

                RunInMainThread([prov, downloader = &ctx.downloader, track, targetDir, print = ctx.print]() {
                    prov->FetchTrackUrl(track.id, [downloader, track, targetDir, print](const std::string& url, bool err) {
                        if (!err && !url.empty()) {
                            downloader->Download(track, url, targetDir);
                            if (print) print("[Загрузка] Начинается скачивание: " + track.artist + " - " + track.title + "\n\n> ");
                        } else {
                            if (print) print("[Ошибка] Не удалось получить аудиопоток для скачивания.\n\n> ");
                        }
                    });
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат номера: df <номер>\n\n> ");
            }
        }
    };

    class LikeCurrentCommand : public IConsoleCommand {
    public:
        void Execute(const std::string&, CommandContext& ctx) override {
            if (!ctx.router) {
                if (ctx.print) ctx.print("[Ошибка] Роутер недоступен.\n\n> ");
                return;
            }

            Track current = ctx.playlist.GetCurrentTrack();
            if (current.id.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Сейчас никакой трек не играет.\n\n> ");
                return;
            }

            std::string title = current.artist + " - " + current.title;
            std::string src = current.source;

            RunInMainThread([router = ctx.router, current, title, src, print = ctx.print]() {
                router->AddTrackToFavorites(current, [print, title, src](bool success, const std::string& err) {
                    if (success) {
                        if (print) print("[Избранное] Текущий трек \"" + title + "\" добавлен в избранное [" + src + "]!\n\n> ");
                    } else {
                        if (print) print("[Ошибка] Не удалось добавить в избранное [" + src + "]: " + err + "\n\n> ");
                    }
                });
            });
        }
    };

    class DislikeCurrentCommand : public IConsoleCommand {
    public:
        void Execute(const std::string&, CommandContext& ctx) override {
            if (!ctx.router) {
                if (ctx.print) ctx.print("[Ошибка] Роутер недоступен.\n\n> ");
                return;
            }

            Track current = ctx.playlist.GetCurrentTrack();
            if (current.id.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Сейчас никакой трек не играет.\n\n> ");
                return;
            }

            std::string title = current.artist + " - " + current.title;
            std::string src = current.source;

            RunInMainThread([router = ctx.router, current, title, src, print = ctx.print]() {
                router->RemoveTrackFromFavorites(current, [print, title, src](bool success, const std::string& err) {
                    if (success) {
                        if (print) print("[Избранное] Трек \"" + title + "\" удален из избранного [" + src + "].\n\n> ");
                    } else {
                        if (print) print("[Ошибка] Не удалось удалить из избранного [" + src + "]: " + err + "\n\n> ");
                    }
                });
            });
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

    // Онлайн поиск и избранное
    commands["find"] = std::make_unique<FindCommand>();
    commands["f"] = std::make_unique<FindCommand>();
    commands["playfind"] = std::make_unique<PlayFindCommand>();
    commands["pf"] = std::make_unique<PlayFindCommand>();
    commands["likefind"] = std::make_unique<LikeFindCommand>();
    commands["lf"] = std::make_unique<LikeFindCommand>();
    commands["addfind"] = std::make_unique<AddFindCommand>();
    commands["af"] = std::make_unique<AddFindCommand>();
    commands["dlfind"] = std::make_unique<DlFindCommand>();
    commands["df"] = std::make_unique<DlFindCommand>();
    commands["like"] = std::make_unique<LikeCurrentCommand>();
    commands["fav"] = std::make_unique<LikeCurrentCommand>();
    commands["dislike"] = std::make_unique<DislikeCurrentCommand>();
    commands["unfav"] = std::make_unique<DislikeCurrentCommand>();
}
