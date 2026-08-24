#pragma once
#include <QObject>
#include <atomic>
#include <string>
#include "models/Track.h"

class IAudioEngine;
class PlaylistManager;
class IAudioProvider;
class NetworkStreamer;

class PlaybackController : public QObject {
    Q_OBJECT
public:
    PlaybackController(IAudioEngine& audio, PlaylistManager& playlist, NetworkStreamer& streamer, QObject* parent = nullptr);

    void SetCurrentProvider(IAudioProvider* provider);
    void SetCrossfadeEnabled(bool enabled);
    void SetSavedPosition(double pos);
    void SetStartPaused(bool paused) { m_startPaused = paused; }

    void AttemptPlay(const Track& track, int attempt = 1);
    void HandleTrackFinished();
    void HandleTrackNearEnd();
    void ClearState();

private:
    IAudioEngine& m_audio;
    PlaylistManager& m_playlist;
    NetworkStreamer& m_streamer;
    IAudioProvider* m_currentProvider = nullptr;

    bool m_crossfadeEnabled = false;
    double m_savedPosition = 0.0;

    int m_skipCount = 0;
    std::atomic<int> m_playbackGeneration{0};

    Track m_preloadedTrack;
    std::string m_cachedNextUrl = "";

    bool m_startPaused = false;
};