#include "CommandDispatcher.h"
#include "PlaybackCommands.h"
#include "VolumeCommands.h"
#include "PlaylistCommands.h"
#include "SystemCommands.h"
#include "utils/platform/DialogService.h"
#include "core/shazam/AudioCaptureService.h"

#include <algorithm>
#include <cctype>

CommandDispatcher::CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist,
                                     DatabaseManager& dbManager, TrackDownloader& downloader,
                                     LyricsFetcher& lyricsFetcher,
                                     IDialogService* dialogService,
                                     IAudioCaptureService* audioCapture,
                                     QNetworkAccessManager* networkManager)
    : m_audio(audio), m_playlist(playlist), m_dbManager(dbManager),
      m_downloader(downloader), m_lyricsFetcher(lyricsFetcher),
      m_ownedDialogService(dialogService ? nullptr : std::make_unique<DialogService>()),
      m_ownedAudioCapture(audioCapture ? nullptr : std::make_unique<AudioCaptureService>()),
      m_dialogService(dialogService ? *dialogService : *m_ownedDialogService),
      m_audioCapture(audioCapture ? *audioCapture : *m_ownedAudioCapture),
      m_networkManager(networkManager) {
    RegisterCommands();
}

CommandDispatcher::~CommandDispatcher() = default;

void CommandDispatcher::SetCurrentProvider(IAudioProvider* provider) {
    m_currentProvider = provider;
}

void CommandDispatcher::SetPrintCallback(std::function<void(const std::string&)> printCb) {
    m_printCb = printCb;
}

void CommandDispatcher::Print(const std::string& msg) {
    if (m_printCb) m_printCb(msg);
}

void CommandDispatcher::RegisterCommands() {
    RegisterPlaybackCommands(m_commands);
    RegisterVolumeCommands(m_commands);
    RegisterPlaylistCommands(m_commands);
    RegisterSystemCommands(m_commands);
}

void CommandDispatcher::Dispatch(const std::string& input) {
    if (input.empty()) return;

    std::string cmd;
    std::string arg;
    size_t spacePos = input.find(' ');

    if (spacePos != std::string::npos) {
        cmd = input.substr(0, spacePos);
        arg = input.substr(spacePos + 1);
    } else {
        cmd = input;
    }
    for (char& c : cmd) c = std::tolower(c);

    auto it = m_commands.find(cmd);
    if (it != m_commands.end()) {
        CommandContext ctx {
            m_audio,
            m_playlist,
            m_dbManager,
            m_downloader,
            m_lyricsFetcher,
            m_currentProvider,
            m_dialogService,
            m_audioCapture,
            m_networkManager,
            m_printCb,
            OnSourceChangeRequested,
            OnGaplessModeChanged,
            OnVisualizerToggled,
            OnQuitRequested,
            OnLogoutRequested,
            OnReloadUiRequested,
            OnSelectPlaylistRequested,
            OnSelectPlaylistToPlayRequested
        };
        it->second->Execute(arg, ctx);
    } else {
        Print("[Ошибка] Неизвестная команда. Введи 'h' для справки.\n\n> ");
    }
}