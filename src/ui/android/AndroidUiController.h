#pragma once

#include "ui/IUiController.h"
#include <QQmlApplicationEngine>
#include <memory>

class ApplicationCore;

namespace ui::android {

class AndroidPlayerBridge;
class TrackListModel;

class AndroidUiController : public IUiController {
    Q_OBJECT
public:
    explicit AndroidUiController(ApplicationCore& core, QObject* parent = nullptr);
    ~AndroidUiController() override;

    void Start() override;
    void Stop() override;
    void SetStatusMessage(const std::string& msg) override;
    void SetCurrentProvider(IAudioProvider* provider) override;
    void SetWaitingAuth(bool waiting) override;
    void OnTrackChanged(const Track& track) override;
    void OnAudioFetched(const std::vector<Track>& tracks) override;
    void OnFinishedFetching() override;

    ApplicationCore& GetCore() { return m_core; }
    AndroidPlayerBridge* GetBridge() const { return m_bridge; }
    TrackListModel* GetTrackModel() const { return m_trackModel; }

private:
    ApplicationCore& m_core;
    QQmlApplicationEngine m_qmlEngine;
    AndroidPlayerBridge* m_bridge{nullptr};
    TrackListModel* m_trackModel{nullptr};
    TrackListModel* m_previewTrackModel{nullptr};
    bool m_started{false};

    void ProcessIntentExtras();
};

} // namespace ui::android
