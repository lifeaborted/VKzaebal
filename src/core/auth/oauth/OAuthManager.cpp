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

    if (service == "VK") {
        GetSavedTokens("VK", [token](const std::vector<std::string>& existing) {
            QJsonArray arr;
            arr.append(QString::fromStdString(token));
            for (const auto& oldTok : existing
                     | std::views::filter([&token](const std::string& t) { return t != token; })
                     | std::views::take(9)) {
                arr.append(QString::fromStdString(oldTok));
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

void OAuthManager::RefreshVkTokenSilently(const std::string& cookies,
                                          const QString& authUrl,
                                          std::function<void(const std::string& newToken, bool success)> callback) {
    if (cookies.empty()) {
        Logger::Log(LogLevel::WARNING, "auth: RefreshVkTokenSilently called with empty cookies.");
        if (callback) callback("", false);
        return;
    }

    if (!m_netManager) {
        m_netManager = new QNetworkAccessManager(this);
        m_ownsNetManager = true;
    }

    auto doRequest = [this, callback](auto self, const QString& targetUrl, const QString& currentCookies, int redirectsRemaining) -> void {
        if (redirectsRemaining < 0) {
            Logger::Log(LogLevel::WARNING, "auth: Silent VK refresh exceeded maximum redirects.");
            if (callback) callback("", false);
            return;
        }

        QUrl url(targetUrl);
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(10000);
        request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        request.setRawHeader("Cookie", currentCookies.toUtf8());

        Logger::Log(LogLevel::INFO, "auth: Silent VK refresh sending request to: " + targetUrl.toStdString());

        QNetworkReply* reply = m_netManager->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply, self, targetUrl, currentCookies, redirectsRemaining, callback]() {
            reply->deleteLater();

            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            Logger::Log(LogLevel::INFO, "auth: Silent VK refresh response HTTP " + std::to_string(statusCode) + " from: " + targetUrl.toStdString());

            QString location = QString::fromUtf8(reply->rawHeader("Location"));
            QUrl redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            QString redirectStr = redirectTarget.isValid() ? redirectTarget.toString() : "";
            QString replyUrl = reply->url().toString();
            QString bodyStr = QString::fromUtf8(reply->readAll());

            QString combined = location + " " + redirectStr + " " + replyUrl + " " + bodyStr;

            QRegularExpression re("access_token=([^&#\\s]+)");
            QRegularExpressionMatch match = re.match(combined);

            if (match.hasMatch()) {
                std::string newToken = match.captured(1).toStdString();
                Logger::Log(LogLevel::INFO, "auth: Silent VK refresh SUCCESS! New token: " + newToken.substr(0, 6) + "...");
                SaveToken(newToken, "VK");
                SaveCookies(currentCookies.toStdString(), "VK");
                if (callback) callback(newToken, true);
                return;
            }

            QString nextUrl = !location.isEmpty() ? location : redirectStr;
            if (!nextUrl.isEmpty() && (statusCode >= 300 && statusCode < 400)) {
                QUrl resolvedUrl = QUrl(targetUrl).resolved(QUrl(nextUrl));
                Logger::Log(LogLevel::INFO, "auth: Following silent auth redirect to: " + resolvedUrl.toString().toStdString());

                QString updatedCookies = currentCookies;
                const auto rawHeaders = reply->rawHeaderPairs();
                for (const auto& pair : rawHeaders) {
                    if (pair.first.toLower() == "set-cookie") {
                        QString cookieVal = QString::fromUtf8(pair.second).split(';').first().trimmed();
                        if (!cookieVal.isEmpty()) {
                            updatedCookies += "; " + cookieVal;
                        }
                    }
                }

                self(self, resolvedUrl.toString(), updatedCookies, redirectsRemaining - 1);
                return;
            }

            Logger::Log(LogLevel::WARNING, "auth: Silent VK refresh failed for: " + targetUrl.toStdString() + ". HTTP: " + std::to_string(statusCode) + ", Location: " + location.toStdString());

            // Try alternate domain (.ru <-> .com) on the primary oauth host only
            QUrl currentTarget(targetUrl);
            QString altEndpoint;
            if (currentTarget.host() == "oauth.vk.com") {
                currentTarget.setHost("oauth.vk.ru");
                altEndpoint = currentTarget.toString();
            } else if (currentTarget.host() == "oauth.vk.ru") {
                currentTarget.setHost("oauth.vk.com");
                altEndpoint = currentTarget.toString();
            }

            if (!altEndpoint.isEmpty() && altEndpoint != targetUrl) {
                Logger::Log(LogLevel::INFO, "auth: Retrying silent VK refresh with alternate endpoint: " + altEndpoint.toStdString());
                self(self, altEndpoint, currentCookies, 3);
            } else {
                if (callback) callback("", false);
            }
        });
    };

    doRequest(doRequest, authUrl, QString::fromStdString(cookies), 5);
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