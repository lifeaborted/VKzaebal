#include "Manager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <QFile>

#include <QRegularExpression>

Manager::Manager(QObject* parent) : QObject(parent) {
    Logger::Log(LogLevel::INFO, "auth (WebView Auth) created.");
}

Manager::~Manager() {}

std::string Manager::GetSavedToken() const {
    QFile file(PathManager::GetTokenFilePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        return in.readLine().trimmed().toStdString();
    }
    return "";
}

void Manager::SaveToken(const std::string& token) const {
    QFile file(PathManager::GetTokenFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << QString::fromStdString(token);
        Logger::Log(LogLevel::INFO, "auth: Token saved to local file.");
    }
}

void Manager::ClearSavedToken() const {
    QFile file(PathManager::GetTokenFilePath());
    if (file.exists()) {
        file.remove();
        Logger::Log(LogLevel::INFO, "auth: Token file removed.");
    }
}

void Manager::onUrlIntercepted(const QString& urlStr) {
    // Ищем токен в адресной строке
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