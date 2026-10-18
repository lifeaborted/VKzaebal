#include "AndroidUiController.h"
#include "AndroidPlayerBridge.h"
#include "TrackListModel.h"
#include "core/ApplicationCore.h"
#include "utils/logger/Logger.h"

#include <QQmlContext>
#include <QQuickStyle>
#include <QCoreApplication>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QtCore/qnativeinterface.h>
#endif

namespace ui::android {

AndroidUiController::AndroidUiController(ApplicationCore& core, QObject* parent)
    : IUiController(parent)
    , m_core(core) {

    m_trackModel = new TrackListModel(this);
    m_previewTrackModel = new TrackListModel(this);
    m_trackModel->SetPreviewModel(m_previewTrackModel);
    m_bridge = new AndroidPlayerBridge(core, *m_trackModel, this);
}

AndroidUiController::~AndroidUiController() = default;

void AndroidUiController::Start() {
    if (m_started) return;
    m_started = true;

    Logger::Log(LogLevel::INFO, "AndroidUiController: Initializing QML interface...");

    m_qmlEngine.rootContext()->setContextProperty("player", m_bridge);
    m_qmlEngine.rootContext()->setContextProperty("trackModel", m_trackModel);
    m_qmlEngine.rootContext()->setContextProperty("previewTrackModel", m_previewTrackModel);

    const QUrl url(QStringLiteral("qrc:/ui/android/qml/Main.qml"));
    connect(&m_qmlEngine, &QQmlApplicationEngine::objectCreated,
            this, [this, url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl) {
                    Logger::Log(LogLevel::ERROR, "AndroidUiController: Failed to load QML root object!");
                    QCoreApplication::exit(-1);
                } else {
                    Logger::Log(LogLevel::INFO, "AndroidUiController: QML root object loaded successfully.");
                    ProcessIntentExtras();
                }
            }, Qt::QueuedConnection);

    m_qmlEngine.load(url);
}

void AndroidUiController::ProcessIntentExtras() {
#if defined(Q_OS_ANDROID)
    if (!QNativeInterface::QAndroidApplication::isActivityContext()) return;
    QJniObject activity(QNativeInterface::QAndroidApplication::context().object());
    if (!activity.isValid()) return;

    QJniObject intent = activity.callObjectMethod("getIntent", "()Landroid/content/Intent;");
    if (!intent.isValid()) return;

    QJniObject extras = intent.callObjectMethod("getExtras", "()Landroid/os/Bundle;");
    if (!extras.isValid()) return;

    auto getStringExtra = [&](const char* key) -> QString {
        QJniObject jKey = QJniObject::fromString(key);
        QJniObject jVal = extras.callObjectMethod("getString", "(Ljava/lang/String;)Ljava/lang/String;", jKey.object<jstring>());
        if (jVal.isValid()) {
            return jVal.toString();
        }
        return QString();
    };

    QString vkLogin = getStringExtra("vk_login");
    QString vkPass = getStringExtra("vk_pass");
    QString vkCode = getStringExtra("vk_code");
    QString vkToken = getStringExtra("vk_token");
    QString yaToken = getStringExtra("ya_token");

    if (!vkToken.isEmpty()) {
        Logger::Log(LogLevel::INFO, "AndroidUiController: Received vk_token from intent extra!");
        m_bridge->saveToken("VK", vkToken);
    } else if (!vkLogin.isEmpty()) {
        Logger::Log(LogLevel::INFO, "AndroidUiController: Received vk_login from intent extra: " + vkLogin.toStdString());
        m_bridge->startVkLogin(vkLogin, vkPass);
    }

    if (!vkCode.isEmpty()) {
        Logger::Log(LogLevel::INFO, "AndroidUiController: Received vk_code from intent extra!");
        m_bridge->submitVkCode(vkCode);
    }

    if (!yaToken.isEmpty()) {
        Logger::Log(LogLevel::INFO, "AndroidUiController: Received ya_token from intent extra!");
        m_bridge->saveToken("Yandex", yaToken);
    }
#endif
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
