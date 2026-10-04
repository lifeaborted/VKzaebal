#include "ui/UiFactory.h"
#include "core/ApplicationCore.h"
#include "core/audio/IAudioEngine.h"
#include "utils/logger/Logger.h"

#ifdef ENABLE_FTXUI
#include "ui/tui/TuiController.h"
#endif

#include "ui/console/core/ConsoleController.h"
#include "ui/console/light/LightConsoleController.h"
#include "core/auth/router/SourceRouter.h"

std::unique_ptr<IUiController> UiFactory::Create(UiMode mode, ApplicationCore& core) {
    switch (mode) {
        case UiMode::TUI: {
#ifdef ENABLE_FTXUI
            Logger::Log(LogLevel::INFO, "UiFactory: Creating TUI controller (FTXUI)");
            return std::make_unique<tui::TuiController>(
                core.GetAudio(),
                core.GetPlaylist(),
                core.GetRouter(),
                core.GetDbManager(),
                core.GetDownloader(),
                core.GetLyricsFetcher(),
                core.GetPlaybackCtrl(),
                core.GetNetworkManager(),
                core.GetConfigService()
            );
#else
            Logger::Log(LogLevel::WARNING, "UiFactory: TUI (FTXUI) was disabled at build time. Falling back to CLI mode.");
            [[fallthrough]];
#endif
        }
        case UiMode::CLI: {
            Logger::Log(LogLevel::INFO, "UiFactory: Creating CLI controller (UltimateRenderer)");
            OAuthManager* authMgr = core.GetAuthManager();
            if (!authMgr) {
                Logger::Log(LogLevel::ERROR, "UiFactory: OAuthManager is null when creating ConsoleController");
                return nullptr;
            }
            auto console = std::make_unique<ConsoleController>(
                core.GetAudio(),
                core.GetPlaylist(),
                *authMgr,
                core.GetDbManager(),
                core.GetDownloader(),
                core.GetLyricsFetcher(),
                core.GetNetworkManager()
            );
            console->SetSourceRouter(&core.GetRouter());
            return console;
        }
        case UiMode::CLI_LIGHT: {
            Logger::Log(LogLevel::INFO, "UiFactory: Creating CLI_LIGHT controller (Lightweight Line CLI)");
            return std::make_unique<LightConsoleController>(
                core.GetAudio(),
                core.GetPlaylist(),
                core.GetDbManager(),
                core.GetDownloader(),
                core.GetLyricsFetcher(),
                core.GetNetworkManager()
            );
        }
    }
    return nullptr;
}
