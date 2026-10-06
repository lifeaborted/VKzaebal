#include "TuiController.h"
#include "ui/tui/TuiTheme.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/auth/router/SourceRouter.h"
#include "core/api/IAudioProvider.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "services/network/NetworkStreamer.h"
#include "core/audio/playback/PlaybackController.h"
#include "ui/console/commands/CommandDispatcher.h"
#include "services/config/ConfigurationService.h"
#include "ui/tui/services/TuiTrackService.h"
#include "utils/logger/Logger.h"

#include <ftxui/dom/linear_gradient.hpp>
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include <algorithm>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace tui {

namespace {
class AutoHideCursorNode : public ftxui::Node {
public:
    explicit AutoHideCursorNode(ftxui::Element child) : m_child(std::move(child)) {}

    void ComputeRequirement() override {
        m_child->ComputeRequirement();
        requirement_ = m_child->requirement();
    }

    void SetBox(ftxui::Box box) override {
        m_child->SetBox(box);
        box_ = box;
    }

    void Render(ftxui::Screen& screen) override {
        screen.SetCursor(ftxui::Screen::Cursor{0, 0, ftxui::Screen::Cursor::Hidden});
        m_child->Render(screen);
    }

private:
    ftxui::Element m_child;
};

inline ftxui::Element AutoHideCursor(ftxui::Element child) {
    return std::make_shared<AutoHideCursorNode>(std::move(child));
}
} // namespace

TuiController::TuiController(
    IAudioEngine& audio,
    PlaylistManager& playlist,
    SourceRouter& router,
    DatabaseManager& dbManager,
    TrackDownloader& downloader,
    LyricsFetcher& lyricsFetcher,
    PlaybackController& playbackCtrl,
    QNetworkAccessManager* networkManager,
    ConfigurationService* configService,
    QObject* parent
) : IUiController(parent),
    m_audio(audio),
    m_playlist(playlist),
    m_router(router),
    m_dbManager(dbManager),
    m_downloader(downloader),
    m_lyricsFetcher(lyricsFetcher),
    m_playbackCtrl(playbackCtrl),
    m_netManager(networkManager),
    m_configService(configService),
    m_screen(ftxui::ScreenInteractive::Fullscreen()) {

    m_coverRenderer = std::make_unique<CoverArtRenderer>(m_netManager, this);
    connect(m_coverRenderer.get(), &CoverArtRenderer::CoverReady, this, [this](const std::string&) {
        m_screen.PostEvent(ftxui::Event::Custom);
    });

    m_trackService = std::make_unique<TuiTrackService>(m_router, m_dbManager, m_downloader, m_playlist, m_configService);

    SetupComponents();
    WireCallbacks();

    m_spectrumTimer = new QTimer(this);
    connect(m_spectrumTimer, &QTimer::timeout, this, &TuiController::OnSpectrumTick);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        if (m_nowPlayingScreen) {
            m_nowPlayingScreen->SetStatusMessage("");
            m_screen.PostEvent(ftxui::Event::Custom);
        }
    });
}

TuiController::~TuiController() {
    Stop();
}

void TuiController::SetupComponents() {
    // 0. Initialize Config
    m_themeConfig = std::make_unique<TuiThemeConfig>(this);
    m_themeConfig->Initialize();
    m_currentTheme = m_themeConfig->GetTheme();
    auto visCfg = m_themeConfig->GetVisualizerConfig();

    // 1. Shared Sidebar
    m_sidebar = std::make_shared<SidebarComponent>();
    m_sidebar->UpdateTheme(m_currentTheme);

    // Load playlists from DB into sidebar
    auto plInfos = m_dbManager.GetPlaylists();
    std::vector<std::string> plNames;
    for (const auto& pl : plInfos) {
        if (IsUserPlaylist(pl.name)) {
            plNames.push_back(pl.name);
        }
    }
    m_sidebar->SetPlaylists(plNames);

    // Restore saved source in Sidebar & SearchScreen
    std::string initialSource = m_configService ? m_configService->GetActiveSource() : "VK";
    std::string sidebarId = initialSource;
    if (sidebarId.rfind("Custom:", 0) == 0) {
        sidebarId = sidebarId.substr(7);
    }
    m_sidebar->SetSelectedId(sidebarId);

    // 2. Screens
    m_nowPlayingScreen = std::make_shared<NowPlayingScreen>(m_audio, m_playlist, *m_coverRenderer, m_sidebar);
    m_nowPlayingScreen->UpdateTheme(m_currentTheme, visCfg);
    m_nowPlayingScreen->SetShowBottomBar(m_bottomBar.IsVisible());

    m_searchScreen = std::make_shared<SearchScreen>(m_audio, m_playlist, *m_coverRenderer, m_sidebar);
    m_searchScreen->UpdateTheme(m_currentTheme);
    m_searchScreen->SetSearchSource(sidebarId);
    m_searchScreen->SetShowBottomBar(m_bottomBar.IsVisible());
    m_sidebar->SetShowBottomBar(m_bottomBar.IsVisible());

    // 3. Modals & ModalManager
    m_playlistModal = std::make_shared<PlaylistModalComponent>();
    m_addToPlaylistModal = std::make_shared<AddToPlaylistModalComponent>();
    if (m_configService) {
        m_settingsModal = std::make_shared<SettingsModalComponent>(*m_configService, *m_themeConfig);
        m_settingsModal->OnToggleBottomBarRequested = [this]() { ToggleBottomBar(); };
        m_settingsModal->OnSettingsChanged = [this]() { m_screen.PostEvent(ftxui::Event::Custom); };
    }
    m_helpModal = std::make_shared<HelpModalComponent>();
    m_helpModal->OnToggleBottomBarRequested = [this]() { ToggleBottomBar(); };

    m_modalManager.RegisterModal(m_playlistModal);
    m_modalManager.RegisterModal(m_addToPlaylistModal);
    if (m_settingsModal) m_modalManager.RegisterModal(m_settingsModal);
    m_modalManager.RegisterModal(m_helpModal);
    m_modalManager.UpdateTheme(m_currentTheme);
    m_modalManager.SetShowBottomBar(m_bottomBar.IsVisible());

    // Wire live config reload
    m_themeConfig->OnConfigChanged = [this]() {
        m_currentTheme = m_themeConfig->GetTheme();
        auto vis = m_themeConfig->GetVisualizerConfig();
        if (m_sidebar) m_sidebar->UpdateTheme(m_currentTheme);
        if (m_nowPlayingScreen) m_nowPlayingScreen->UpdateTheme(m_currentTheme, vis);
        if (m_searchScreen) m_searchScreen->UpdateTheme(m_currentTheme);
        m_modalManager.UpdateTheme(m_currentTheme);
        m_screen.PostEvent(ftxui::Event::Custom);
    };

    ReloadFavoriteIds();

    // 4. Command Dispatcher & Command Bar
    m_dispatcher = std::make_unique<CommandDispatcher>(
        m_audio, m_playlist, m_dbManager, m_downloader, m_lyricsFetcher,
        nullptr, nullptr, m_netManager
    );
    m_dispatcher->SetSourceRouter(&m_router);
    m_dispatcher->SetPrintCallback([this](const std::string& msg) {
        SetStatusMessage(msg);
    });
    m_dispatcher->OnSourceChangeRequested = [this](const std::string& src) {
        m_router.SwitchSource(src);
    };
    m_dispatcher->OnQuitRequested = [this]() {
        emit QuitRequested();
    };
    m_dispatcher->OnLogoutRequested = [this](const std::string& svc) {
        emit LogoutRequested(svc);
    };

    m_commandBar.OnExecuteCommand = [this](const std::string& cmd) {
        if (cmd == "h" || cmd == "help") {
            OpenHelpModal(false);
        } else if (cmd == "info" || cmd == "about" || cmd == "sys") {
            OpenHelpModal(true);
        } else if (cmd == "settings" || cmd == "cfg" || cmd == "config") {
            OpenSettingsModal();
        } else if (m_dispatcher) {
            m_dispatcher->Dispatch(cmd);
            if (cmd.rfind("pl", 0) == 0) {
                ReloadPlaylists();
            }
        }
        m_screen.PostEvent(ftxui::Event::Custom);
    };
    m_commandBar.OnDismiss = [this]() {
        m_screen.PostEvent(ftxui::Event::Custom);
    };

    // 5. Input Router
    TuiInputRouter::Callbacks callbacks;
    callbacks.getActiveScreenIndex = [this]() { return m_activeScreenIndex; };
    callbacks.isTypingInSearch = [this]() {
        return m_activeScreenIndex == 1 && m_searchScreen && m_searchScreen->IsInputActive();
    };
    callbacks.switchScreen = [this](int screenIndex) {
        SwitchScreen(static_cast<ScreenType>(screenIndex));
    };
    callbacks.toggleSearchMode = [this]() {
        if (m_searchScreen) m_searchScreen->ToggleSearchMode();
    };
    callbacks.toggleBottomBar = [this]() {
        ToggleBottomBar();
    };
    callbacks.openPlaylistModal = [this]() {
        OpenPlaylistManagerModal();
    };
    callbacks.openHelpModal = [this](bool showSystemInfo) {
        OpenHelpModal(showSystemInfo);
    };
    callbacks.openSettingsModal = [this]() {
        OpenSettingsModal();
    };
    callbacks.cycleVisualizerMode = [this]() {
        if (m_nowPlayingScreen) m_nowPlayingScreen->CycleVisualizerMode();
    };
    callbacks.toggleLike = [this]() {
        Track cur = m_playlist.GetCurrentTrack();
        if (!cur.id.empty()) ToggleLikeForTrack(cur);
    };
    callbacks.quitRequested = [this]() {
        m_screen.Exit();
    };

    m_inputRouter = std::make_unique<TuiInputRouter>(
        m_modalManager, m_commandBar, m_audio, m_playlist, m_configService, m_screen, std::move(callbacks)
    );

    // 6. Containers
    m_tabContainer = ftxui::Container::Tab({m_nowPlayingScreen, m_searchScreen}, &m_activeScreenIndex);
    m_rootContainer = ftxui::Container::Horizontal({m_sidebar, m_tabContainer, m_commandBar.GetComponent()});

    // 7. Main Renderer with Modal Overlay
    auto renderer = ftxui::Renderer(m_rootContainer, [this]() {
        const auto& theme = m_currentTheme;
        auto bgCfg = m_themeConfig->GetBackgroundConfig();

        // Screen Body: Sidebar on the left, Tab Container on the right!
        ftxui::Element screenBody = ftxui::hbox({
            m_sidebar->Render(),
            ftxui::separatorLight() | ftxui::color(theme.border),
            m_tabContainer->Render() | ftxui::flex
        }) | ftxui::flex;

        bool hasBottom = (m_commandBar.IsActive() || m_bottomBar.IsVisible());
        ftxui::Element bottomElement;
        if (m_commandBar.IsActive()) {
            bottomElement = m_commandBar.Render(theme);
        } else if (m_bottomBar.IsVisible()) {
            bottomElement = m_bottomBar.RenderForScreen(m_activeScreenIndex, theme);
        }

        std::vector<ftxui::Element> rootChildren;
        rootChildren.push_back(std::move(screenBody) | ftxui::flex);
        if (hasBottom) {
            rootChildren.push_back(ftxui::separatorLight() | ftxui::color(theme.border));
            rootChildren.push_back(std::move(bottomElement));
            rootChildren.push_back(ftxui::text(""));
        }

        auto rootElem = ftxui::vbox(std::move(rootChildren)) | ftxui::flex;

        ftxui::Element finalElem;
        if (bgCfg.mode == BackgroundMode::GRADIENT && bgCfg.gradientStops.size() >= 2) {
            ftxui::LinearGradient grad;
            grad.Angle(90.0f);
            for (const auto& stop : bgCfg.gradientStops) {
                grad.Stop(ftxui::Color::RGB(stop.r, stop.g, stop.b));
            }
            finalElem = rootElem | ftxui::bgcolor(grad);
        } else {
            finalElem = rootElem | ftxui::bgcolor(bgCfg.color);
        }

        return m_modalManager.RenderOverlay(std::move(finalElem), AutoHideCursor);
    });

    m_mainComponent = ftxui::CatchEvent(renderer, [this](ftxui::Event event) {
        return m_inputRouter->RouteEvent(event);
    });
}

void TuiController::WireCallbacks() {
    // 1. Sidebar selection (unified across both screens)
    m_sidebar->SetOnSelectCallback([this](const std::string& id, bool isPlaylist) {
        if (m_searchScreen) {
            m_searchScreen->SetSearchSource(id);
        }

        if (m_activeScreenIndex == 0) {
            // Main screen: switch playback source and load its queue
            std::string targetSource = isPlaylist ? ("Custom:" + id) : id;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, targetSource, id, isPlaylist]() {
                emit SourceChanged(targetSource);
                if (isPlaylist) {
                    SetStatusMessage("Загружен плейлист '" + id + "'");
                }
            }, Qt::QueuedConnection);
        } else if (m_activeScreenIndex == 1 && m_searchScreen) {
            // Search screen: ONLY change search source and re-run search query if present, NEVER change playback queue or auto-play!
            if (m_searchScreen->OnPerformSearch) {
                m_searchScreen->OnPerformSearch(
                    m_searchScreen->GetSearchQuery(),
                    id,
                    m_searchScreen->GetSearchMode()
                );
            }
        }

        m_screen.PostEvent(ftxui::Event::Custom);
    });

    m_sidebar->SetOnManagePlaylistsCallback([this]() {
        OpenPlaylistManagerModal();
    });

    m_sidebar->SetOnCreatePlaylistCallback([this]() {
        OpenPlaylistManagerModal();
    });

    m_sidebar->SetOnOpenSettingsCallback([this]() {
        OpenSettingsModal();
    });

    m_sidebar->SetOnOpenHelpCallback([this]() {
        OpenHelpModal(false);
    });

    if (m_playlistModal) {
        m_playlistModal->OnCreatePlaylistRequested = [this](const std::string& name) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, name]() {
                bool ok = m_dbManager.CreatePlaylist(name);
                m_screen.Post([this, name, ok]() {
                    if (ok) {
                        ReloadPlaylists();
                        SetStatusMessage("[Плейлист] Создан: " + name);
                    } else {
                        SetStatusMessage("[Плейлист] Не удалось создать: " + name);
                    }
                    m_screen.PostEvent(ftxui::Event::Custom);
                });
            }, Qt::QueuedConnection);
        };

        m_playlistModal->OnDeletePlaylistRequested = [this](const std::string& name) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, name]() {
                bool ok = m_dbManager.DeletePlaylist(name);
                m_screen.Post([this, name, ok]() {
                    if (ok) {
                        if (m_sidebar && m_sidebar->GetSelectedId() == name) {
                            m_sidebar->SetSelectedId("VK");
                            QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
                                emit SourceChanged("VK");
                            }, Qt::QueuedConnection);
                        }
                        ReloadPlaylists();
                        SetStatusMessage("[Плейлист] Удален: " + name);
                    } else {
                        SetStatusMessage("[Плейлист] Не удалось удалить: " + name);
                    }
                    m_screen.PostEvent(ftxui::Event::Custom);
                });
            }, Qt::QueuedConnection);
        };

        m_playlistModal->OnCloseRequested = [this]() {
            m_screen.PostEvent(ftxui::Event::Custom);
        };
    }

    if (m_addToPlaylistModal) {
        m_addToPlaylistModal->OnAddToPlaylistSelected = [this](int playlistId, const std::string& playlistName, const Track& track) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, playlistId, playlistName, track]() {
                m_dbManager.SaveTracks({track});
                bool ok = m_dbManager.AddTrackToPlaylist(playlistId, track.id);
                m_screen.Post([this, playlistName, track, ok]() {
                    if (ok) {
                        SetStatusMessage("[Плейлист] Добавлено в '" + playlistName + "': " + track.title);
                        std::string activeSource = m_configService ? m_configService->GetActiveSource() : "";
                        if (activeSource == "Custom:" + playlistName) {
                            m_playlist.AddTrack(track);
                        }
                        ReloadPlaylists();
                    } else {
                        SetStatusMessage("[Плейлист] Трек уже есть в плейлисте '" + playlistName + "'");
                    }
                    m_screen.PostEvent(ftxui::Event::Custom);
                });
            }, Qt::QueuedConnection);
        };

        m_addToPlaylistModal->OnCloseRequested = [this]() {
            m_screen.PostEvent(ftxui::Event::Custom);
        };
    }

    if (m_settingsModal) {
        m_settingsModal->OnCloseRequested = [this]() {
            m_screen.PostEvent(ftxui::Event::Custom);
        };
        m_settingsModal->OnSettingsChanged = [this]() {
            m_currentTheme = m_themeConfig->GetTheme();
            auto vis = m_themeConfig->GetVisualizerConfig();
            if (m_sidebar) m_sidebar->UpdateTheme(m_currentTheme);
            if (m_nowPlayingScreen) m_nowPlayingScreen->UpdateTheme(m_currentTheme, vis);
            if (m_searchScreen) m_searchScreen->UpdateTheme(m_currentTheme);
            m_modalManager.UpdateTheme(m_currentTheme);
            m_screen.PostEvent(ftxui::Event::Custom);
        };
    }

    if (m_helpModal) {
        m_helpModal->OnCloseRequested = [this]() {
            m_screen.PostEvent(ftxui::Event::Custom);
        };
    }

    // 2. NowPlayingScreen Callbacks
    m_nowPlayingScreen->OnSeekRequested = [this](double seconds) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, seconds]() {
            m_audio.SetPositionSeconds(seconds);
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnPlayTrackRequested = [this](int trackIndex) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, trackIndex]() {
            m_playlist.JumpToQueueIndex(trackIndex);
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnTogglePlayPauseRequested = [this]() {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            if (m_audio.IsPlaying()) m_audio.Pause();
            else m_audio.Resume();
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnNextTrackRequested = [this]( ) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.Next();
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnPrevTrackRequested = [this]() {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.Previous();
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnVolumeChanged = [this](float volume) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, volume]() {
            m_audio.SetVolume(volume);
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnToggleShuffleRequested = [this]() {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.ToggleShuffle();
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnCycleRepeatRequested = [this]() {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this]() {
            m_playlist.ToggleRepeat();
        }, Qt::QueuedConnection);
    };

    m_nowPlayingScreen->OnToggleLike = [this](const Track& track) {
        ToggleLikeForTrack(track);
    };

    m_nowPlayingScreen->OnAddToPlaylist = [this](const Track& track) {
        OpenAddToPlaylistModal(track);
    };

    m_nowPlayingScreen->OnDownloadTrackRequested = [this](const Track& track) {
        DownloadTrack(track);
    };

    // 3. SearchScreen Callbacks
    m_searchScreen->OnSearchModeChanged = [this](SearchMode mode) {
        if (m_activeScreenIndex == 1) {
            m_sidebar->SetShowPlaylists(mode == SearchMode::LOCAL);
            m_sidebar->SetItemDisabled("Offline", mode == SearchMode::API);
            m_screen.PostEvent(ftxui::Event::Custom);
        }
    };

    m_searchScreen->OnPerformSearch = [this](const std::string& query, const std::string& source, SearchMode mode) {
        QString q = QString::fromStdString(query).trimmed();
        if (q.isEmpty()) {
            m_searchScreen->SetSearchLoading(false);
            m_screen.Post([this]() {
                m_searchScreen->SetSearchResults({}, "");
            });
            m_screen.PostEvent(ftxui::Event::Custom);
            return;
        }

        m_searchScreen->SetSearchLoading(true);
        m_screen.PostEvent(ftxui::Event::Custom);

        if (mode == SearchMode::LOCAL) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, q, source]() {
                std::vector<Track> matched = m_trackService ? m_trackService->SearchLocalTracks(source, q) : std::vector<Track>{};
                m_screen.Post([this, matched]() {
                    m_searchScreen->SetSearchResults(matched, "");
                });
                m_screen.PostEvent(ftxui::Event::Custom);
            }, Qt::QueuedConnection);
        } else {
            std::string qStd = q.toStdString();
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, qStd, source]() {
                m_router.Search(source, qStd, 50, 0, [this](const std::vector<Track>& tracks, const std::string& error) {
                    m_screen.Post([this, tracks, error]() {
                        m_searchScreen->SetSearchResults(tracks, error);
                    });
                    m_screen.PostEvent(ftxui::Event::Custom);
                });
            }, Qt::QueuedConnection);
        }
    };

    m_searchScreen->OnLoadMoreResults = [this](const std::string& query, const std::string& source, int offset) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, query, source, offset]() {
            m_router.Search(source, query, 50, offset, [this](const std::vector<Track>& tracks, const std::string&) {
                m_screen.Post([this, tracks]() {
                    m_searchScreen->AppendSearchResults(tracks);
                });
                m_screen.PostEvent(ftxui::Event::Custom);
            });
        }, Qt::QueuedConnection);
    };

    m_searchScreen->OnPlayTrackNow = [this](const Track& track) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, track]() {
            m_playlist.PlayTrackNow(track);
            m_playbackCtrl.AttemptPlay(track);
            OnTrackChanged(track);
        }, Qt::QueuedConnection);
    };

    m_searchScreen->OnEnqueueTrack = [this](const Track& track) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, track]() {
            m_playlist.AddTrack(track);
            SetStatusMessage("Добавлено в очередь: " + track.title);
            m_screen.PostEvent(ftxui::Event::Custom);
        }, Qt::QueuedConnection);
    };

    m_searchScreen->OnLikeTrack = [this](const Track& track) {
        ToggleLikeForTrack(track);
    };

    m_searchScreen->OnDownloadTrack = [this](const Track& track) {
        DownloadTrack(track);
    };

    m_searchScreen->OnPlayQueueIndexRequested = m_nowPlayingScreen->OnPlayTrackRequested;
    m_searchScreen->OnSeekRequested = m_nowPlayingScreen->OnSeekRequested;
    m_searchScreen->OnTogglePlayPauseRequested = m_nowPlayingScreen->OnTogglePlayPauseRequested;
    m_searchScreen->OnNextTrackRequested = m_nowPlayingScreen->OnNextTrackRequested;
    m_searchScreen->OnPrevTrackRequested = m_nowPlayingScreen->OnPrevTrackRequested;
    m_searchScreen->OnVolumeChanged = m_nowPlayingScreen->OnVolumeChanged;
}

void TuiController::SwitchScreen(ScreenType type) {
    m_activeScreenIndex = static_cast<int>(type);
    if (m_tabContainer) {
        if (type == ScreenType::NOW_PLAYING && m_nowPlayingScreen) {
            if (m_searchScreen) {
                m_searchScreen->DeactivateSearchInput();
            }
            m_tabContainer->SetActiveChild(m_nowPlayingScreen);
            m_sidebar->SetShowPlaylists(true);
            m_sidebar->SetItemDisabled("Offline", false);
        } else if (type == ScreenType::SEARCH && m_searchScreen) {
            m_tabContainer->SetActiveChild(m_searchScreen);
            bool isLocal = (m_searchScreen->GetSearchMode() == SearchMode::LOCAL);
            m_sidebar->SetShowPlaylists(isLocal);
            m_sidebar->SetItemDisabled("Offline", !isLocal);
        }
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::Start() {
    if (m_isRunning) return;
    m_isRunning = true;

#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(hOut, &cursorInfo)) {
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hOut, &cursorInfo);
    }
#endif
    // Switch to alternate screen buffer, clear screen, home, hide cursor
    std::cout << "\033[?1049h\033[2J\033[H\033[?25l" << std::flush;

    // Start 30 FPS ticker (every 33ms) for smooth FFT and terminal-friendly progression
    m_spectrumTimer->start(33);

    // Initial resize/refresh events to ensure proper dimensions immediately
    m_screen.PostEvent(ftxui::Event::Special({0}));
    QTimer::singleShot(50, [this]() {
        m_screen.PostEvent(ftxui::Event::Special({0}));
        m_screen.PostEvent(ftxui::Event::Custom);
    });

    m_tuiThread = std::thread([this]() {
        Logger::Log(LogLevel::INFO, "TUI thread started, running FTXUI Loop...");
        m_screen.Loop(m_mainComponent);
        Logger::Log(LogLevel::INFO, "FTXUI Loop exited.");

        if (m_isRunning) {
            m_isRunning = false;
            QMetaObject::invokeMethod(this, [this]() {
                emit QuitRequested();
            }, Qt::QueuedConnection);
        }
    });
}

void TuiController::Stop() {
    bool wasRunning = m_isRunning.exchange(false);

    std::cout << "\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?1049l\033[?25h" << std::flush;
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    if (GetConsoleCursorInfo(hOut, &cursorInfo)) {
        cursorInfo.bVisible = TRUE;
        SetConsoleCursorInfo(hOut, &cursorInfo);
    }
#endif

    if (m_spectrumTimer) {
        m_spectrumTimer->stop();
    }
    if (m_statusTimer) {
        m_statusTimer->stop();
    }

    m_screen.Exit();

    if (m_tuiThread.joinable()) {
        if (m_tuiThread.get_id() != std::this_thread::get_id()) {
            m_tuiThread.join();
        } else {
            m_tuiThread.detach();
        }
    }
}

void TuiController::SetStatusMessage(const std::string& rawMsg) {
    if (rawMsg.empty()) return;

    if (rawMsg.find("Инициализация") != std::string::npos ||
        rawMsg.find("токен") != std::string::npos ||
        rawMsg.find("сохраненному") != std::string::npos) {
        return;
    }

    // Clean and sanitize string for top-right status bar (flatten newlines, strip asterisks and prompt chars)
    std::string msg;
    msg.reserve(rawMsg.size());
    bool prevSpace = false;
    for (char c : rawMsg) {
        if (c == '\r' || c == '\n') {
            if (!prevSpace && !msg.empty()) {
                msg.push_back(' ');
                prevSpace = true;
            }
        } else if (c == '*' && msg.empty()) {
            continue;
        } else {
            msg.push_back(c);
            prevSpace = (c == ' ');
        }
    }
    while (!msg.empty() && (msg.back() == ' ' || msg.back() == '>' || msg.back() == '*')) {
        msg.pop_back();
    }
    size_t first = msg.find_first_not_of(" \t*");
    if (first != std::string::npos) {
        msg = msg.substr(first);
    } else {
        msg.clear();
    }
    if (msg.empty()) return;

    if (msg.size() > 90) {
        msg = msg.substr(0, 87) + "...";
    }

    if (m_nowPlayingScreen) {
        m_nowPlayingScreen->SetStatusMessage(msg);
    }

    // Safely start timer on Qt thread via invokeMethod
    QMetaObject::invokeMethod(this, [this]() {
        if (m_statusTimer) {
            m_statusTimer->start(3500);
        }
    }, Qt::QueuedConnection);

    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::SetCurrentProvider(IAudioProvider* provider) {
    if (m_dispatcher) {
        m_dispatcher->SetCurrentProvider(provider);
    }
}

void TuiController::OnTrackChanged(const Track& track) {
    m_playlist.SetActiveTrack(track);
    if (m_coverRenderer) {
        m_coverRenderer->RequestCover(track.coverUrl);
    }
    if (m_nowPlayingScreen) {
        m_nowPlayingScreen->SetCurrentTrackLiked(m_favoriteTrackIds.count(track.id) > 0);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::OnAudioFetched(const std::vector<Track>& tracks) {
    for (const auto& t : tracks) {
        if (!t.id.empty()) {
            m_favoriteTrackIds.insert(t.id);
        }
    }
    if (m_searchScreen) {
        m_searchScreen->SetFavoriteTrackIds(m_favoriteTrackIds);
    }
    if (m_nowPlayingScreen) {
        Track cur = m_playlist.GetCurrentTrack();
        m_nowPlayingScreen->SetCurrentTrackLiked(m_favoriteTrackIds.count(cur.id) > 0);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::OnFinishedFetching() {
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::OnSpectrumTick() {
    if (!m_isRunning) return;

    if (m_audio.IsPlaying()) {
        if (m_activeScreenIndex == 0) {
            std::vector<float> fft = m_audio.GetSpectrumData();
            if (m_nowPlayingScreen) {
                m_nowPlayingScreen->UpdateSpectrum(fft);
                if (++m_searchThrottleCounter % 4 == 0) {
                    m_nowPlayingScreen->AdvanceTicker();
                }
            }
            m_screen.PostEvent(ftxui::Event::Custom);
        } else if (m_activeScreenIndex == 1) {
            // On Search screen: advance ticker for smooth marquee text scrolling
            if (m_searchScreen) {
                if (++m_searchThrottleCounter % 4 == 0) {
                    m_searchScreen->AdvanceTicker();
                    m_screen.PostEvent(ftxui::Event::Custom);
                }
            }
        }

        Track cur = m_playlist.GetCurrentTrack();
        if (m_coverRenderer) {
            m_coverRenderer->RequestCover(cur.coverUrl);
        }
    } else {
        if (m_activeScreenIndex == 1 && m_searchScreen) {
            if (++m_searchThrottleCounter % 4 == 0) {
                m_searchScreen->AdvanceTicker();
                m_screen.PostEvent(ftxui::Event::Custom);
            }
        }
        if (m_nowPlayingScreen && m_activeScreenIndex == 0) {
            bool needRefresh = false;
            if (m_nowPlayingScreen->HasActiveSpectrum()) {
                m_nowPlayingScreen->UpdateSpectrum({});
                needRefresh = true;
            }
            if (++m_searchThrottleCounter % 4 == 0) {
                m_nowPlayingScreen->AdvanceTicker();
                needRefresh = true;
            }
            if (needRefresh) {
                m_screen.PostEvent(ftxui::Event::Custom);
            }
        }
    }
}

void TuiController::ReloadPlaylists() {
    auto plInfos = m_dbManager.GetPlaylists();
    std::vector<std::string> plNames;
    std::vector<PlaylistInfo> filtered;
    for (const auto& pl : plInfos) {
        if (IsUserPlaylist(pl.name)) {
            plNames.push_back(pl.name);
            filtered.push_back(pl);
        }
    }
    if (m_sidebar) {
        m_sidebar->SetPlaylists(plNames);
    }
    if (m_playlistModal) {
        m_playlistModal->SetPlaylists(filtered);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::OpenPlaylistManagerModal() {
    auto plInfos = m_dbManager.GetPlaylists();
    std::vector<PlaylistInfo> filtered;
    for (const auto& pl : plInfos) {
        if (IsUserPlaylist(pl.name)) {
            filtered.push_back(pl);
        }
    }
    if (m_playlistModal) {
        m_playlistModal->Show(filtered);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::ReloadFavoriteIds() {
    if (m_trackService) {
        m_favoriteTrackIds = m_trackService->LoadFavoriteTrackIds();
    }
    if (m_searchScreen) {
        m_searchScreen->SetFavoriteTrackIds(m_favoriteTrackIds);
    }
    if (m_nowPlayingScreen) {
        m_nowPlayingScreen->SetFavoriteTrackIds(m_favoriteTrackIds);
        Track cur = m_playlist.GetCurrentTrack();
        m_nowPlayingScreen->SetCurrentTrackLiked(m_favoriteTrackIds.count(cur.id) > 0);
    }
}

void TuiController::ToggleLikeForTrack(const Track& track) {
    if (track.id.empty() || !m_trackService) return;

    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, track]() {
        m_trackService->ToggleLikeForTrack(track, m_favoriteTrackIds, [this, track](bool ok, bool liked, const std::string& msg) {
            m_screen.Post([this, track, ok, liked, msg]() {
                if (ok) {
                    if (m_searchScreen) {
                        m_searchScreen->SetTrackLiked(track.id, liked);
                    }
                    if (m_nowPlayingScreen) {
                        m_nowPlayingScreen->SetTrackLiked(track.id, liked);
                    }
                    Track cur = m_playlist.GetCurrentTrack();
                    if (cur.id == track.id && m_nowPlayingScreen) {
                        m_nowPlayingScreen->SetCurrentTrackLiked(liked);
                    }
                }
                SetStatusMessage(msg);
            });
        });
    }, Qt::QueuedConnection);
}

void TuiController::OpenAddToPlaylistModal(const Track& track) {
    if (track.id.empty()) return;

    auto allPls = m_dbManager.GetPlaylists();
    auto containedIds = m_dbManager.GetPlaylistIdsContainingTrack(track.id);
    std::unordered_set<int> inPls(containedIds.begin(), containedIds.end());

    std::vector<PlaylistSelectionItem> items;
    for (const auto& pl : allPls) {
        if (IsUserPlaylist(pl.name)) {
            PlaylistSelectionItem item;
            item.id = pl.id;
            item.name = pl.name;
            item.trackCount = pl.trackCount;
            item.alreadyContains = (inPls.count(pl.id) > 0);
            items.push_back(item);
        }
    }

    if (m_addToPlaylistModal) {
        m_addToPlaylistModal->Show(track, items);
    }
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::ToggleBottomBar() {
    m_bottomBar.Toggle();
    bool show = m_bottomBar.IsVisible();
    if (m_nowPlayingScreen) m_nowPlayingScreen->SetShowBottomBar(show);
    if (m_searchScreen) m_searchScreen->SetShowBottomBar(show);
    if (m_sidebar) m_sidebar->SetShowBottomBar(show);
    m_modalManager.SetShowBottomBar(show);
    m_screen.PostEvent(ftxui::Event::Custom);
}

void TuiController::OpenSettingsModal() {
    if (m_settingsModal) {
        m_settingsModal->Show();
        m_screen.PostEvent(ftxui::Event::Custom);
    }
}

void TuiController::OpenHelpModal(bool showSystemInfo) {
    if (m_helpModal) {
        m_helpModal->Show(showSystemInfo);
        m_screen.PostEvent(ftxui::Event::Custom);
    }
}

void TuiController::DownloadTrack(const Track& track) {
    if (track.id.empty() || !m_trackService) return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), [this, track]() {
        m_trackService->DownloadTrack(track, [this](const std::string& msg) {
            SetStatusMessage(msg);
            m_screen.PostEvent(ftxui::Event::Custom);
        });
    }, Qt::QueuedConnection);
}

} // namespace tui
