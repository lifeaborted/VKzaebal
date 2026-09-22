#include "OAuthManager.h"
#include "WebViewCookieReader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <qtkeychain/keychain.h>

#include <QCoreApplication>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <ranges>

OAuthManager::OAuthManager(QObject* parent, QNetworkAccessManager* netManager)
    : QObject(parent), m_netManager(netManager) {
    if (!m_netManager) {
        m_netManager = new QNetworkAccessManager(this);
        m_ownsNetManager = true;
    }
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("VKAudioPlayer");
    }
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("VKAudioPlayer");
    }

    Logger::Log(LogLevel::INFO, "auth (OAuth Manager) created. Secure storage initialized.");
}

OAuthManager::~OAuthManager() {
    if (m_ownsNetManager && m_netManager) {
        m_netManager->deleteLater();
        m_netManager = nullptr;
    }
}

void OAuthManager::SaveToken(const std::string& token, const QString& service) const {
    if (token.empty()) return;

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

void OAuthManager::GetSavedTokens(const QString& service, std::function<void(const std::vector<std::string>&)> callback) const {
    auto* job = new QKeychain::ReadPasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("oauth_token");

    connect(job, &QKeychain::Job::finished, [service, callback](QKeychain::Job* baseJob) {
        std::vector<std::string> tokens;
        if (baseJob->error()) {
            if (baseJob->error() != QKeychain::Error::EntryNotFound) {
                Logger::Log(LogLevel::ERROR, "auth: Failed to read tokens for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
            }
            callback(tokens);
        } else {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(baseJob);
            QString data = readJob->textData().trimmed();
            if (data.startsWith("[")) {
                QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
                if (doc.isArray()) {
                    for (const auto& val : doc.array()) {
                        QString str = val.toString().trimmed();
                        if (!str.isEmpty()) tokens.push_back(str.toStdString());
                    }
                }
            } else if (!data.isEmpty()) {
                tokens.push_back(data.toStdString());
            }
            callback(tokens);
        }
    });

    job->start();
}

void OAuthManager::GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const {
    GetSavedTokens(service, [callback](const std::vector<std::string>& tokens) {
        callback(tokens.empty() ? "" : tokens.front());
    });
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

    ClearSavedCookies(service);

    // Удаляем связанные cookies и веб-кэш для данного сервиса
    WebViewCookieReader::ClearServiceCache(service.toStdString());
}

void OAuthManager::SaveCookies(const std::string& cookies, const QString& service) const {
    if (cookies.empty()) return;

    auto* job = new QKeychain::WritePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("session_cookies");
    job->setTextData(QString::fromStdString(cookies));

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to securely save cookies for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Session cookies securely saved for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::GetSavedCookies(const QString& service, std::function<void(const std::string&)> callback) const {
    auto* job = new QKeychain::ReadPasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("session_cookies");

    connect(job, &QKeychain::Job::finished, [service, callback](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            if (baseJob->error() != QKeychain::Error::EntryNotFound) {
                Logger::Log(LogLevel::ERROR, "auth: Failed to read cookies for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
            }
            callback("");
        } else {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(baseJob);
            std::string cookies = readJob ? readJob->textData().trimmed().toStdString() : "";
            callback(cookies);
        }
    });

    job->start();
}

void OAuthManager::ClearSavedCookies(const QString& service) const {
    auto* job = new QKeychain::DeletePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("session_cookies");

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error() && baseJob->error() != QKeychain::Error::EntryNotFound) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to delete cookies for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Session cookies securely removed for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::SaveUserId(const std::string& uid, const QString& service) const {
    if (uid.empty()) return;

    auto* job = new QKeychain::WritePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("user_id");
    job->setTextData(QString::fromStdString(uid));

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to securely save user ID for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: User ID securely cached for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::GetSavedUserId(const QString& service, std::function<void(const std::string&)> callback) const {
    auto* job = new QKeychain::ReadPasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("user_id");

    connect(job, &QKeychain::Job::finished, [service, callback](QKeychain::Job* baseJob) {
        if (baseJob->error()) {
            if (baseJob->error() != QKeychain::Error::EntryNotFound) {
                Logger::Log(LogLevel::ERROR, "auth: Failed to read cached user ID for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
            }
            callback("");
        } else {
            auto* readJob = qobject_cast<QKeychain::ReadPasswordJob*>(baseJob);
            std::string uid = readJob ? readJob->textData().trimmed().toStdString() : "";
            callback(uid);
        }
    });

    job->start();
}

void OAuthManager::ClearSavedUserId(const QString& service) const {
    auto* job = new QKeychain::DeletePasswordJob(service);
    job->setAutoDelete(true);
    job->setKey("user_id");

    connect(job, &QKeychain::Job::finished, [service](QKeychain::Job* baseJob) {
        if (baseJob->error() && baseJob->error() != QKeychain::Error::EntryNotFound) {
            Logger::Log(LogLevel::ERROR, "auth: Failed to delete cached user ID for " + service.toStdString() + ": " + baseJob->errorString().toStdString());
        } else {
            Logger::Log(LogLevel::INFO, "auth: Cached user ID removed for " + service.toStdString());
        }
    });

    job->start();
}

void OAuthManager::onUrlIntercepted(const QString& urlStr) {
    bool isVkCallback = urlStr.startsWith("https://oauth.vk.com/blank.html") ||
                        urlStr.startsWith("https://oauth.vk.ru/blank.html");

    bool isSpotifyCallback = urlStr.startsWith("http://127.0.0.1:8080/callback");

    bool isYandexCallback = urlStr.startsWith("https://music.yandex.ru/") ||
                            urlStr.startsWith("https://oauth.yandex.ru/");

    if (!isVkCallback && !isSpotifyCallback && !isYandexCallback) {
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

void OAuthManager::onYtAuthIntercepted(const QString& cookies) {
    Logger::Log(LogLevel::INFO, "auth: YouTube Auth successful via JS-Sniper! Intercepted session cookies (length: " + std::to_string(cookies.size()) + ")");
    std::string token = cookies.toStdString();
    SaveToken(token, "YouTube");
    emit YtAuthSucceeded(token);
}