#include "VolumeCommands.h"
#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <cmath>
#include <string>

namespace {
    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

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
}

void RegisterVolumeCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands) {
    commands["+"] = std::make_unique<VolumeAdjustCommand>(0.1f);
    commands["-"] = std::make_unique<VolumeAdjustCommand>(-0.1f);
    commands["v"] = std::make_unique<VolumeSetCommand>();
    commands["cv"] = std::make_unique<VolumeCurrentCommand>();
}
