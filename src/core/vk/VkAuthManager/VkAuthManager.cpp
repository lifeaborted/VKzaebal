#include "VkAuthManager.h"
#include "utils/logger/logger.h"

#include <QQmlContext>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <iostream>

const QString TOKEN_FILE = ".vk_token";

VkAuthManager::VkAuthManager(QObject* parent) : QObject(parent) {
    Logger::Log(LogLevel::INFO, "VkAuthManager (Native WebView) created.");
}

VkAuthManager::~VkAuthManager() {
    if (m_view) {
        m_view->deleteLater();
    }
}

std::string VkAuthManager::GetSavedToken() const {
    QFile file(TOKEN_FILE);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        return in.readLine().trimmed().toStdString();
    }
    return "";
}

void VkAuthManager::SaveToken(const std::string& token) const {
    QFile file(TOKEN_FILE);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << QString::fromStdString(token);
        Logger::Log(LogLevel::INFO, "VkAuthManager: Token saved to local file.");
    }
}

void VkAuthManager::Authenticate(int appId) {
    if (!m_view) {
        m_view = new QQuickView();
        m_view->setTitle("Авторизация ВКонтакте");
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->resize(500, 600);
    }

    QUrl authUrl("https://oauth.vk.com/authorize");
    QUrlQuery query;
    query.addQueryItem("client_id", QString::number(appId));
    query.addQueryItem("display", "page");
    query.addQueryItem("redirect_uri", "https://oauth.vk.com/blank.html");
    query.addQueryItem("scope", "audio,offline");
    query.addQueryItem("response_type", "token");
    query.addQueryItem("v", "5.199");
    authUrl.setQuery(query);

    m_view->rootContext()->setContextProperty("cppAuthUrl", authUrl.toString());
    m_view->rootContext()->setContextProperty("cppAuthManager", this);

    m_view->setSource(QUrl("qrc:/core/vk/VkAuthManager/auth.qml"));

    // Вывод ошибок QML, если они вдруг появятся
    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors()) {
            std::cout << "\n[QML ОШИБКА] " << error.toString().toStdString() << "\n";
        }
    }

    Logger::Log(LogLevel::INFO, "VkAuthManager: Opening Native WebView...");
    std::cout << "\n[ВНИМАНИЕ] Открывается окно браузера... Пройдите авторизацию там.\n";

    m_view->show();
}

void VkAuthManager::onUrlIntercepted(const QString& urlString) {
    if (urlString.startsWith("https://oauth.vk.com/blank.html")) {
        m_view->hide();

        QRegularExpression re("access_token=([a-zA-Z0-9_-]+)");
        QRegularExpressionMatch match = re.match(urlString);

        if (match.hasMatch()) {
            std::string token = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "VkAuthManager: Token intercepted successfully!");
            emit TokenReceived(token);
        } else {
            Logger::Log(LogLevel::ERROR, "VkAuthManager: Failed to parse token.");
            emit AuthFailed("Failed to parse token from URL");
        }

        m_view->deleteLater();
        m_view = nullptr;
    }
}