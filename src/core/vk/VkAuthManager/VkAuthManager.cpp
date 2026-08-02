#include "VkAuthManager.h"
#include "utils/logger/logger.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

const QString TOKEN_FILE = ".vk_token";

VkAuthManager::VkAuthManager(QObject* parent) : QObject(parent) {
    Logger::Log(LogLevel::INFO, "VkAuthManager (WebView Auth) created.");
}

VkAuthManager::~VkAuthManager() {}

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

void VkAuthManager::onUrlIntercepted(const QString& urlStr) {
    // Ищем токен в адресной строке
    if (urlStr.contains("access_token=")) {
        // ИЗМЕНЕННАЯ РЕГУЛЯРКА: берем всё от access_token= и до первого амперсанда & или конца строки
        QRegularExpression re("access_token=([^&]+)");
        QRegularExpressionMatch match = re.match(urlStr);

        if (match.hasMatch()) {
            std::string token = match.captured(1).toStdString();

            // Выводим в лог кусок токена, чтобы убедиться, что точки на месте
            Logger::Log(LogLevel::INFO, "VkAuthManager: Token intercepted from URL! Starts with: " + token.substr(0, 6) + "...");

            emit TokenReceived(token);
        }
    } else if (urlStr.contains("error=")) {
        Logger::Log(LogLevel::ERROR, "VkAuthManager: Auth failed or denied.");
        emit AuthFailed("URL auth error.");
    }
}