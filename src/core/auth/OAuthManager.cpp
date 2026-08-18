#include "OAuthManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <QSettings>

#include <QRegularExpression>

OAuthManager::OAuthManager(QObject* parent) : QObject(parent) {
    Logger::Log(LogLevel::INFO, "auth (WebView Auth) created.");
}

OAuthManager::~OAuthManager() {}

std::string OAuthManager::GetSavedToken(const QString& service) const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("General/" + service.toLower() + "_token", "").toString().toStdString();
}

void OAuthManager::SaveToken(const std::string& token, const QString& service) const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("General/" + service.toLower() + "_token", QString::fromStdString(token));
    settings.sync();
    Logger::Log(LogLevel::INFO, "auth: Token saved for " + service.toStdString());
}

void OAuthManager::ClearSavedToken(const QString& service) const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.remove("General/" + service.toLower() + "_token");
    settings.sync();
    Logger::Log(LogLevel::INFO, "auth: Token removed for " + service.toStdString());
}

void OAuthManager::onUrlIntercepted(const QString& urlStr) {
    bool isVkCallback = urlStr.startsWith("https://oauth.vk.com/blank.html") ||
                        urlStr.startsWith("https://oauth.vk.ru/blank.html");

    bool isSpotifyCallback = urlStr.startsWith("http://127.0.0.1:8080/callback");

    if (!isVkCallback && !isSpotifyCallback) {
        // Игнорируем внутренние редиректы
        return;
    }

    if (urlStr.contains("access_token=")) {
        QRegularExpression re("access_token=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string token = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "auth: Token intercepted from URL! Starts with: " + token.substr(0, 6) + "...");
            emit TokenReceived(token);
        }
    }
    else if (urlStr.contains("code=")) {
        QRegularExpression re("code=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string code = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "auth: Spotify Authorization Code intercepted!");
            emit AuthCodeReceived(code);
        }
    }
    else if (urlStr.contains("error=")) {
        Logger::Log(LogLevel::ERROR, "auth: Auth failed or denied.");
        emit AuthFailed("URL auth error.");
    }
}