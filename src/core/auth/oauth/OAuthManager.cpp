#include "OAuthManager.h"
#include "WebViewCookieReader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include <qtkeychain/keychain.h>

#include <QCoreApplication>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>

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
    if (token.empty()) return;

    if (service == "VK") {
        GetSavedTokens("VK", [token](const std::vector<std::string>& existing) {
            QJsonArray arr;
            arr.append(QString::fromStdString(token));
            int count = 1;
            for (const auto& oldTok : existing) {
                if (oldTok != token && count < 10) {
                    arr.append(QString::fromStdString(oldTok));
                    count++;
                }
            }
            QJsonDocument doc(arr);
            QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

            auto* job = new QKeychain::WritePasswordJob("VK");
            job->setAutoDelete(true);
            job->setKey("oauth_token");
            job->setTextData(jsonStr);

            connect(job, &QKeychain::Job::finished, [](QKeychain::Job* baseJob) {
                if (baseJob->error()) {
                    Logger::Log(LogLevel::ERROR, "auth: Failed to securely save VK tokens: " + baseJob->errorString().toStdString());
                } else {
                    Logger::Log(LogLevel::INFO, "auth: VK token pool securely updated");
                }
            });

            job->start();
        });
        return;
    }

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

    // Удаляем связанные cookies и веб-кэш для данного сервиса
    WebViewCookieReader::ClearServiceCache(service.toStdString());
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
    Logger::Log(LogLevel::INFO, "auth: YouTube Auth successful via JS-Sniper! Attempting full session extraction...");
    std::string token = WebViewCookieReader::GetFullYouTubeCookies();
    if (token.empty() || token.find("LOGIN_INFO=") == std::string::npos) {
        Logger::Log(LogLevel::WARNING, "auth: SQLite cookie extraction didn't yield LOGIN_INFO, falling back to JS cookies.");
        token = cookies.toStdString();
    } else {
        Logger::Log(LogLevel::INFO, "auth: Full YouTube session cookies successfully extracted (length: " + std::to_string(token.size()) + ")");
    }
    SaveToken(token, "YouTube");
    emit YtAuthSucceeded(token);
}