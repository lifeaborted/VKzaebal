#include "AndroidUiController.h"
#include "AndroidPlayerBridge.h"
#include "TrackListModel.h"
#include "core/ApplicationCore.h"
#include "utils/logger/Logger.h"

#include <QQmlContext>
#include <QQuickStyle>
#include <QCoreApplication>

namespace ui::android {

AndroidUiController::AndroidUiController(ApplicationCore& core, QObject* parent)
    : IUiController(parent)
    , m_core(core) {

    m_trackModel = new TrackListModel(this);
    m_bridge = new AndroidPlayerBridge(core, *m_trackModel, this);
}

AndroidUiController::~AndroidUiController() = default;

void AndroidUiController::Start() {
    if (m_started) return;
    m_started = true;

    Logger::Log(LogLevel::INFO, "AndroidUiController: Initializing QML interface...");

    m_qmlEngine.rootContext()->setContextProperty("player", m_bridge);
    m_qmlEngine.rootContext()->setContextProperty("trackModel", m_trackModel);

    const QUrl url(QStringLiteral("qrc:/ui/android/qml/Main.qml"));
    connect(&m_qmlEngine, &QQmlApplicationEngine::objectCreated,
            this, [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl) {
                    Logger::Log(LogLevel::ERROR, "AndroidUiController: Failed to load QML root object!");
                    QCoreApplication::exit(-1);
                } else {
                    Logger::Log(LogLevel::INFO, "AndroidUiController: QML root object loaded successfully.");
                }
            }, Qt::QueuedConnection);

    m_qmlEngine.load(url);
}

void AndroidUiController::Stop() {
    Logger::Log(LogLevel::INFO, "AndroidUiController: Stopping...");
}

void AndroidUiController::SetStatusMessage(const std::string& msg) {
    if (m_bridge) {
        m_bridge->SetStatus(QString::fromStdString(msg));
    }
}

void AndroidUiController::SetCurrentProvider(IAudioProvider* provider) {
    Q_UNUSED(provider);
}

void AndroidUiController::SetWaitingAuth(bool waiting) {
    Q_UNUSED(waiting);
}

void AndroidUiController::OnTrackChanged(const Track& track) {
    if (m_bridge) {
        m_bridge->UpdateCurrentTrack(track);
    }
}

void AndroidUiController::OnAudioFetched(const std::vector<Track>& tracks) {
    if (m_bridge) {
        m_bridge->OnAudioFetched(tracks);
    }
}

void AndroidUiController::OnFinishedFetching() {
    if (m_bridge) {
        m_bridge->OnFinishedFetching();
    }
}

} // namespace ui::android
