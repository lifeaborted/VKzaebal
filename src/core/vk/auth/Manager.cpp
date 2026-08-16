#include "Manager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <QSettings> // ДОБАВЛЕНО ВМЕСТО QFile

#include <QRegularExpression>

Manager::Manager(QObject* parent) : QObject(parent) {
    Logger::Log(LogLevel::INFO, "auth (WebView Auth) created.");
}

Manager::~Manager() {}

std::string Manager::GetSavedToken() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("General/vk_token", "").toString().toStdString();
}

void Manager::SaveToken(const std::string& token) const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("General/vk_token", QString::fromStdString(token));
    settings.sync();

    Logger::Log(LogLevel::INFO, "auth: Token saved to config.ini.");
}

void Manager::ClearSavedToken() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.remove("General/vk_token");
    settings.sync();

    Logger::Log(LogLevel::INFO, "auth: Token removed from config.ini.");
}

void Manager::onUrlIntercepted(const QString& urlStr) {
    if (urlStr.contains("access_token=")) {
        QRegularExpression re("access_token=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string token = match.captured(1).toStdString();
            Logger::Log(LogLevel::INFO, "auth: Token intercepted from URL! Starts with: " + token.substr(0, 6) + "...");

            emit TokenReceived(token);
        }
    } else if (urlStr.contains("error=")) {
        Logger::Log(LogLevel::ERROR, "auth: Auth failed or denied.");
        emit AuthFailed("URL auth error.");
    }
}