#include "OAuthManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <qtkeychain/keychain.h>

#include <QCoreApplication>
#include <QRegularExpression>

OAuthManager::OAuthManager(QObject* parent) : QObject(parent) {
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("VKAudioPlayer");
    }
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("VKAudioPlayer");
    }

    Logger::Log(LogLevel::INFO, "auth (OAuth Manager) created. Secure storage initialized.");
}

OAuthManager::~OAuthManager() {}

void OAuthManager::SaveToken(const std::string& token, const QString& service) const {
    auto* job = new QKeychain::WritePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");
    job->setTextData(QString::fromStdString(token));

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to securely save token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Token securely saved for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const {
    auto* job = new QKeychain::ReadPasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");

    connect(job, &QKeychain::Job::finished, [service, callback](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            if (baseJob->error() != QKeychain::Error::EntryNotFound) {
                Logger::Log(LogLevel::ERROR, "auth: Failed to read token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
            }
            callback("");
        } else {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(baseJob);
            callback(readJob->textData().toStdString());
        }
    });

    job->start();
}

void OAuthManager::ClearSavedToken(const QString& service) const {
    auto* job = new QKeychain::DeletePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error() && baseJob->error() != QKeychain::Error::EntryNotFound) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to delete token for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Token securely removed for " + service.toStdString());
        }
    });

    job->start();
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

void OAuthManager::onScTokenIntercepted(const QString& tokenStr) {
    QString cleanToken = tokenStr;
    if (cleanToken.startsWith('"') && cleanToken.endsWith('"')) {
        cleanToken = cleanToken.mid(1, cleanToken.length() - 2);
    }

    Logger::Log(LogLevel::INFO, "auth: SC Token intercepted via JS-Sniper!");
    emit TokenReceived(cleanToken.toStdString());
}