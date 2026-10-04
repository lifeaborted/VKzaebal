#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>

#include "models/Track.h"
#include "ui/tui/CoverArtRenderer.h"
#include "ui/tui/TuiThemeConfig.h"
#include "ui/tui/components/SidebarComponent.h"
#include "ui/tui/components/PlaylistModalComponent.h"
#include "ui/tui/components/AddToPlaylistModalComponent.h"
#include "ui/tui/components/SettingsModalComponent.h"
#include "ui/tui/components/HelpModalComponent.h"
#include "ui/tui/screens/NowPlayingScreen.h"
#include "ui/tui/screens/SearchScreen.h"

#include "ui/IUiController.h"

class IAudioEngine;
class PlaylistManager;
class SourceRouter;
class DatabaseManager;
class TrackDownloader;
class LyricsFetcher;
class PlaybackController;
class IAudioProvider;
class QNetworkAccessManager;
class QTimer;
class CommandDispatcher;
class ConfigurationService;

namespace tui {

enum class ScreenType {
    NOW_PLAYING = 0,
    SEARCH = 1
};

class TuiController : public IUiController {
    Q_OBJECT
public:
    TuiController(
        IAudioEngine& audio,
        PlaylistManager& playlist,
        SourceRouter& router,
        DatabaseManager& dbManager,
        TrackDownloader& downloader,
        LyricsFetcher& lyricsFetcher,
        PlaybackController& playbackCtrl,
        QNetworkAccessManager* networkManager = nullptr,
        ConfigurationService* configService = nullptr,
        QObject* parent = nullptr
    );
    ~TuiController() override;

    void Start() override;
    void Stop() override;

    void SetStatusMessage(const std::string& msg) override;
    void SetCurrentProvider(IAudioProvider* provider) override;
    void OnTrackChanged(const Track& track) override;
    void OnAudioFetched(const std::vector<Track>& tracks) override;
    void OnFinishedFetching() override;
    void PostCustomEvent() { m_screen.PostEvent(ftxui::Event::Custom); }

    // QuitRequested, SourceChanged, OfflineModeRequested, LogoutRequested
    // are inherited from IUiController

private slots:
    void OnSpectrumTick();

private:
    void SetupComponents();
    void WireCallbacks();
    void SwitchScreen(ScreenType type);
    void ToggleLikeForTrack(const Track& track);
    void ReloadFavoriteIds();
    void ReloadPlaylists();
    void OpenPlaylistManagerModal();
    void OpenAddToPlaylistModal(const Track& track);
    void OpenSettingsModal();
    void OpenHelpModal(bool showSystemInfo = false);
    void RebuildBottomBars(const ThemePalette& theme);
    void ToggleBottomBar();
    void DownloadTrack(const Track& track);
    void AddTrackToPlaylist(const Track& track);

    std::unordered_set<std::string> m_favoriteTrackIds;

    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    SourceRouter& m_router;
    DatabaseManager& m_dbManager;
    TrackDownloader& m_downloader;
    LyricsFetcher& m_lyricsFetcher;
    PlaybackController& m_playbackCtrl;
    QNetworkAccessManager* m_netManager = nullptr;
    ConfigurationService* m_configService = nullptr;

    std::unique_ptr<TuiThemeConfig> m_themeConfig;
    ThemePalette m_currentTheme;

    std::unique_ptr<CoverArtRenderer> m_coverRenderer;
    std::shared_ptr<SidebarComponent> m_sidebar;
    std::shared_ptr<NowPlayingScreen> m_nowPlayingScreen;
    std::shared_ptr<SearchScreen> m_searchScreen;
    std::shared_ptr<PlaylistModalComponent> m_playlistModal;
    std::shared_ptr<AddToPlaylistModalComponent> m_addToPlaylistModal;
    std::shared_ptr<SettingsModalComponent> m_settingsModal;
    std::shared_ptr<HelpModalComponent> m_helpModal;

    int m_activeScreenIndex = 0;
    ftxui::Component m_tabContainer;
    ftxui::Component m_rootContainer;
    ftxui::Component m_mainComponent;
    ftxui::Element m_nowPlayingBottomBar;
    ftxui::Element m_searchBottomBar;

    std::unique_ptr<CommandDispatcher> m_dispatcher;
    bool m_isCommandMode = false;
    bool m_showBottomBar = true;
    std::string m_commandInputText;
    ftxui::Component m_commandInputComponent;

    std::atomic<bool> m_isRunning{false};
    ftxui::ScreenInteractive m_screen;
    std::thread m_tuiThread;
    QTimer* m_spectrumTimer = nullptr;
    QTimer* m_statusTimer = nullptr;

    std::string m_lastTrackCoverUrl;
    QTimer* m_coverDebounceTimer = nullptr;
    std::string m_pendingCoverUrl;
    int m_searchThrottleCounter = 0;
};

} // namespace tui
