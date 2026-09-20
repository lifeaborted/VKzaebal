#include "PlaybackCommands.h"
#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

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

    class SeekCommand : public IConsoleCommand {
        static std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }

        static bool ParseDoubleStrict(const std::string& str, double& outVal) {
            std::string s = Trim(str);
            if (s.empty()) return false;
            try {
                size_t idx = 0;
                double val = std::stod(s, &idx);
                if (idx != s.length()) return false;
                outVal = val;
                return true;
            } catch (...) {
                return false;
            }
        }

        static bool ParseTime(const std::string& rawStr, double& outSeconds) {
            std::string str = Trim(rawStr);
            if (str.empty()) return false;

            size_t firstColon = str.find(':');
            if (firstColon != std::string::npos) {
                size_t secondColon = str.find(':', firstColon + 1);
                if (secondColon != std::string::npos) {
                    double hours = 0.0, mins = 0.0, secs = 0.0;
                    if (!ParseDoubleStrict(str.substr(0, firstColon), hours) ||
                        !ParseDoubleStrict(str.substr(firstColon + 1, secondColon - firstColon - 1), mins) ||
                        !ParseDoubleStrict(str.substr(secondColon + 1), secs)) {
                        return false;
                    }
                    if (hours < 0 || mins < 0 || mins >= 60 || secs < 0 || secs >= 60) return false;
                    outSeconds = hours * 3600.0 + mins * 60.0 + secs;
                    return true;
                } else {
                    double mins = 0.0, secs = 0.0;
                    if (!ParseDoubleStrict(str.substr(0, firstColon), mins) ||
                        !ParseDoubleStrict(str.substr(firstColon + 1), secs)) {
                        return false;
                    }
                    if (mins < 0 || secs < 0 || secs >= 60) return false;
                    outSeconds = mins * 60.0 + secs;
                    return true;
                }
            } else {
                double val = 0.0;
                if (!ParseDoubleStrict(str, val) || val < 0) return false;
                outSeconds = val;
                return true;
            }
        }

        static std::string FormatTime(double seconds) {
            int totalSec = static_cast<int>(seconds);
            if (totalSec < 0) totalSec = 0;
            int hrs = totalSec / 3600;
            int mins = (totalSec % 3600) / 60;
            int secs = totalSec % 60;
            char buf[32];
            if (hrs > 0) {
                std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", hrs, mins, secs);
            } else {
                std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
            }
            return std::string(buf);
        }

        void Execute(const std::string& arg, CommandContext& ctx) override {
            double pos = 0.0;
            if (ParseTime(arg, pos)) {
                RunInMainThread([ctx, pos]() { ctx.audio.SetPositionSeconds(pos); });
                if (ctx.print) {
                    ctx.print("[Перемотка] Переход на " + FormatTime(pos) + " (" + std::to_string(static_cast<int>(pos)) + " сек.)\n\n> ");
                }
            } else {
                if (ctx.print) {
                    ctx.print("[Ошибка] Неверный формат времени. Используй: seek 1:30 (или seek 90)\n\n> ");
                }
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
                ctx.dbManager.ClearSourceSession(src);
            });
            if (ctx.print) ctx.print("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
        }
    };

    class SavePosCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                int mode = std::stoi(arg);
                if (mode >= 0 && mode <= 2) {
                    QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Playback/SavePosition", mode);
                    std::string msg;
                    if (mode == 0) {
                        msg = "[Режим] Сохранение позиции отключено (всегда сначала).\n\n> ";
                    } else if (mode == 1) {
                        msg = "[Режим] Теперь плеер запоминает только трек (время с 0:00).\n\n> ";
                    } else {
                        msg = "[Режим] Теперь плеер запоминает трек и точную позицию по времени.\n\n> ";
                    }
                    if (ctx.print) ctx.print(msg);
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Используй: savepos 0 (выкл), savepos 1 (только трек), savepos 2 (трек + время)\n\n> ");
                }
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: savepos 0, 1 или 2\n\n> ");
            }
        }
    };
}

void RegisterPlaybackCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands) {
    commands["p"] = std::make_unique<PlayPauseCommand>();
    commands["n"] = std::make_unique<NextCommand>();
    commands["b"] = std::make_unique<PrevCommand>();
    commands["seek"] = std::make_unique<SeekCommand>();
    commands["r"] = std::make_unique<RepeatCommand>();
    commands["j"] = std::make_unique<JumpCommand>();
    commands["sh"] = std::make_unique<ShuffleCommand>();
    commands["st"] = std::make_unique<StandardOrderCommand>();
    commands["rs"] = std::make_unique<ResetSessionCommand>();
    commands["reset"] = std::make_unique<ResetSessionCommand>();
    commands["savepos"] = std::make_unique<SavePosCommand>();
}
