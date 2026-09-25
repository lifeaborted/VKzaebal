#include "VkAuthService.h"
#include "VkDeviceManager.h"
#include "utils/logger/Logger.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace {
constexpr const char* kVkApiBase = "https://api.vk.ru";
}

VkAuthService::VkAuthService(QObject* parent, QNetworkAccessManager* netManager)
    : QObject(parent), m_netManager(netManager) {
    if (!m_netManager) {
        m_netManager = new QNetworkAccessManager(this);
        m_ownsNetManager = true;
    }
}

VkAuthService::~VkAuthService() {
    if (m_ownsNetManager && m_netManager) {
        m_netManager->deleteLater();
        m_netManager = nullptr;
    }
}

void VkAuthService::StartLogin(const QString& login, const QString& password) {
    m_login = login.trimmed();
    // Normalize phone number (remove spaces, dashes, brackets if starting with + or digits)
    if (m_login.startsWith("+") || (!m_login.isEmpty() && m_login[0].isDigit())) {
        m_login.remove(' ');
        m_login.remove('-');
        m_login.remove('(');
        m_login.remove(')');
    }

    m_savedPassword = password;
    m_savedCode.clear();
    m_sid.clear();
    m_validationSid.clear();
    m_flowName.clear();
    m_verificationMethod.clear();
    m_needPassword = false;
    m_lastCaptchaSid.clear();
    m_lastCaptchaKey.clear();
    m_lastSuccessToken.clear();

    emit StatusChanged("Получение анонимного токена устройства...");
    FetchAnonymToken([this](bool ok, const QString& err) {
        if (!ok) {
            emit AuthError("Ошибка получения анонимного токена: " + err);
            return;
        }
        emit StatusChanged("Проверка учетной записи VK...");
        ValidateAccount();
    });
}

void VkAuthService::FetchAnonymToken(std::function<void(bool, const QString&)> callback) {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_anonymToken.isEmpty() && now < m_anonymTokenExpiry - 300) {
        callback(true, "");
        return;
    }

    QUrl url(QString(kVkApiBase) + "/oauth/get_anonym_token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAuthUserAgent().toUtf8());
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery body;
    body.addQueryItem("client_id", VkDeviceManager::kClientId);
    body.addQueryItem("client_secret", VkDeviceManager::kClientSecret);
    body.addQueryItem("app_id", VkDeviceManager::kClientId);
    body.addQueryItem("v", VkDeviceManager::kApiVersionAuth);
    body.addQueryItem("lang", "ru");
    body.addQueryItem("https", "1");
    body.addQueryItem("device_id", VkDeviceManager::Instance().GetDeviceId());

    QByteArray postData = body.query(QUrl::FullyEncoded).toUtf8();
    postData.replace("+", "%2B");

    QNetworkReply* reply = m_netManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        if (root.contains("token")) {
            m_anonymToken = root["token"].toString();
            m_anonymTokenExpiry = root["expired_at"].toInteger();
            Logger::Log(LogLevel::INFO, "VkAuthService: Anonym token acquired successfully.");
            callback(true, "");
        } else {
            callback(false, "Отсутствует токен в ответе сервера");
        }
    });
}

void VkAuthService::ValidateAccount() {
    QUrl url(QString(kVkApiBase) + "/method/auth.validateAccount");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAuthUserAgent().toUtf8());
    request.setRawHeader("Authorization", "Bearer " + m_anonymToken.toUtf8());
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery body;
    body.addQueryItem("login", m_login);
    body.addQueryItem("force_password", "0");
    body.addQueryItem("passkey_supported", "0");
    body.addQueryItem("supported_ways", "callreset,codegen,email,reserve_code,password,push,sms");
    body.addQueryItem("flow_type", "auth_without_password");
    body.addQueryItem("sak_version", "1.112");
    body.addQueryItem("api_id", VkDeviceManager::kClientId);

    QString trustedHash = VkDeviceManager::Instance().GetTrustedHash();
    if (!trustedHash.isEmpty()) {
        body.addQueryItem("accounts_trusted_hashes", trustedHash);
    }

    body.addQueryItem("device_id", VkDeviceManager::Instance().GetDeviceId());
    body.addQueryItem("v", VkDeviceManager::kApiVersionAuth);
    body.addQueryItem("lang", "ru");
    body.addQueryItem("https", "1");

    if (!m_lastCaptchaSid.isEmpty() && !m_lastCaptchaKey.isEmpty()) {
        body.addQueryItem("captcha_sid", m_lastCaptchaSid);
        body.addQueryItem("captcha_key", m_lastCaptchaKey);
    }
    if (!m_lastSuccessToken.isEmpty()) {
        body.addQueryItem("success_token", m_lastSuccessToken);
    }

    QByteArray postData = body.query(QUrl::FullyEncoded).toUtf8();
    postData.replace("+", "%2B");

    QNetworkReply* reply = m_netManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        if (root.contains("error")) {
            QJsonObject err = root["error"].toObject();
            int errCode = err["error_code"].toInt();

            if (errCode == 14 || err["error_msg"].toString().contains("Captcha")) {
                QString redirectUri = err["redirect_uri"].toString();
                QString captchaImg = err["captcha_img"].toString();
                QString captchaSid = QString::number(err["captcha_sid"].toInteger());
                if (captchaSid == "0" && err["captcha_sid"].isString()) {
                    captchaSid = err["captcha_sid"].toString();
                }

                Logger::Log(LogLevel::WARNING, "VkAuthService: Captcha required! redirectUri=" + redirectUri.toStdString());
                emit CaptchaRequired(redirectUri, captchaImg, captchaSid);
                return;
            }

            emit AuthError("Ошибка проверки аккаунта: " + err["error_msg"].toString());
            return;
        }

        QJsonObject resp = root["response"].toObject();
        m_sid = resp["sid"].toString();
        m_flowName = resp["flow_name"].toString();

        if (m_sid.isEmpty() && m_flowName == "need_registration") {
            emit AuthError("Аккаунт с таким логином не найден в VK.");
            return;
        }

        QJsonObject nextStep = resp["next_step"].toObject();
        m_verificationMethod = nextStep["verification_method"].toString();
        m_needPassword = nextStep["need_password"].toBool();
        QString phoneMask = resp["phone_mask"].toString();

        Logger::Log(LogLevel::INFO, "VkAuthService: ValidateAccount succeeded. sid=" + m_sid.toStdString() +
                                    ", method=" + m_verificationMethod.toStdString() +
                                    ", need_password=" + (m_needPassword ? "true" : "false"));

        // Case 1: Account requires password
        if (m_verificationMethod == "password" || m_needPassword || m_flowName.contains("password")) {
            if (m_savedPassword.isEmpty()) {
                emit PasswordRequired(m_sid);
                return;
            }
            if (!m_verificationMethod.isEmpty() && m_verificationMethod != "password" && m_savedCode.isEmpty()) {
                emit CodeRequired(m_verificationMethod, m_sid, phoneMask);
                return;
            }
            RequestToken();
            return;
        }

        // Case 2: Method is 2FA / push / codegen / sms without password
        if (!m_verificationMethod.isEmpty()) {
            if (m_savedCode.isEmpty()) {
                emit CodeRequired(m_verificationMethod, m_sid, phoneMask);
                return;
            }
            RequestToken();
            return;
        }

        // Fallback: ask for password if nothing specified
        if (m_savedPassword.isEmpty()) {
            emit PasswordRequired(m_sid);
        } else {
            RequestToken();
        }
    });
}

void VkAuthService::SubmitPassword(const QString& password) {
    m_savedPassword = password;
    emit StatusChanged("Пароль принят...");

    // If 2FA method was requested and code not yet entered, ask for code
    if (!m_verificationMethod.isEmpty() && m_verificationMethod != "password" && m_savedCode.isEmpty()) {
        emit CodeRequired(m_verificationMethod, m_sid, "");
        return;
    }

    RequestToken();
}

void VkAuthService::SubmitCode(const QString& code) {
    m_savedCode = code.trimmed();
    emit StatusChanged("Код принят...");

    // If password is required and not yet provided, ask for password
    if (m_needPassword && m_savedPassword.isEmpty()) {
        emit PasswordRequired(m_sid);
        return;
    }

    RequestToken();
}

void VkAuthService::SubmitCaptcha(const QString& captchaSid, const QString& captchaKey, const QString& successToken) {
    m_lastCaptchaSid = captchaSid;
    m_lastCaptchaKey = captchaKey;
    m_lastSuccessToken = successToken;

    emit StatusChanged("Повторная валидация после капчи...");
    ValidateAccount();
}

void VkAuthService::SubmitCaptchaKey(const QString& captchaKey) {
    SubmitCaptcha(m_lastCaptchaSid, captchaKey, "");
}

void VkAuthService::RequestToken() {
    QUrl url(QString(kVkApiBase) + "/oauth/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAuthUserAgent().toUtf8());
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery body;
    body.addQueryItem("scope", "all");
    body.addQueryItem("client_id", VkDeviceManager::kClientId);
    body.addQueryItem("client_secret", VkDeviceManager::kClientSecret);
    body.addQueryItem("username", m_login);
    body.addQueryItem("2fa_supported", "1");
    body.addQueryItem("libverify_support", "1");
    body.addQueryItem("device_trusted_hash_support", "1");
    body.addQueryItem("supported_ways", "push,email");
    body.addQueryItem("sak_version", "1.142");
    body.addQueryItem("v", VkDeviceManager::kApiVersionAuth);
    body.addQueryItem("https", "1");
    body.addQueryItem("api_id", VkDeviceManager::kClientId);
    body.addQueryItem("lang", "ru");
    body.addQueryItem("device_id", VkDeviceManager::Instance().GetDeviceId());
    body.addQueryItem("anonymous_token", m_anonymToken);

    if (!m_flowName.isEmpty()) {
        body.addQueryItem("flow_type", m_flowName);
    }

    // Determine sid to use (validation_sid from need_validation takes priority)
    QString sidToUse = !m_validationSid.isEmpty() ? m_validationSid : m_sid;
    if (!sidToUse.isEmpty()) {
        body.addQueryItem("sid", sidToUse);
    }

    // Set grant_type and credentials
    if (!m_savedPassword.isEmpty()) {
        body.addQueryItem("grant_type", "password");
        body.addQueryItem("password", m_savedPassword);
        if (!m_savedCode.isEmpty()) {
            body.addQueryItem("code", m_savedCode);
        }
    } else {
        if (m_verificationMethod == "sms" || m_verificationMethod == "callreset") {
            body.addQueryItem("grant_type", "phone_confirmation_sid");
        } else {
            body.addQueryItem("grant_type", "without_password");
            body.addQueryItem("password", "");
        }
        if (!m_savedCode.isEmpty()) {
            body.addQueryItem("code", m_savedCode);
        }
    }

    if (!m_lastCaptchaSid.isEmpty() && !m_lastCaptchaKey.isEmpty()) {
        body.addQueryItem("captcha_sid", m_lastCaptchaSid);
        body.addQueryItem("captcha_key", m_lastCaptchaKey);
    }
    if (!m_lastSuccessToken.isEmpty()) {
        body.addQueryItem("success_token", m_lastSuccessToken);
    }

    QByteArray postData = body.query(QUrl::FullyEncoded).toUtf8();
    postData.replace("+", "%2B");

    QNetworkReply* reply = m_netManager->post(request, postData);
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        if (root.contains("error")) {
            QString errStr = root["error"].toString();
            int errCode = root["error_code"].toInt();

            // 1. Captcha
            if (errStr == "need_captcha" || errCode == 14) {
                QString redirectUri = root["redirect_uri"].toString();
                QString captchaImg = root["captcha_img"].toString();
                QString captchaSid = QString::number(root["captcha_sid"].toInteger());
                if (captchaSid == "0" && root["captcha_sid"].isString()) {
                    captchaSid = root["captcha_sid"].toString();
                }
                emit CaptchaRequired(redirectUri, captchaImg, captchaSid);
                return;
            }

            // 2. 2FA Validation Required ("need_validation" or "need_authcheck")
            if (errStr == "need_validation" || errStr == "need_authcheck") {
                m_validationSid = root["validation_sid"].toString();
                if (m_validationSid.isEmpty()) {
                    m_validationSid = root["sid"].toString();
                }
                QString valType = root["validation_type"].toString();
                if (valType.isEmpty()) valType = "2fa";
                QString mask = root["phone_mask"].toString();

                Logger::Log(LogLevel::INFO, "VkAuthService: 2FA required (need_validation), type=" + valType.toStdString());
                emit CodeRequired(valType, m_validationSid, mask);
                return;
            }

            // Clean credentials on final error
            m_savedPassword.clear();
            m_savedCode.clear();

            // 3. Other errors
            QString desc = root["error_description"].toString();
            if (desc.isEmpty() && root.contains("ban_info")) {
                desc = root["ban_info"].toObject()["message"].toString();
            }
            if (desc.isEmpty()) desc = errStr;

            emit AuthError("Ошибка авторизации: " + desc);
            return;
        }

        // Clean credentials on success
        m_savedPassword.clear();
        m_savedCode.clear();

        if (root.contains("access_token")) {
            std::string token = root["access_token"].toString().toStdString();
            int userId = root["user_id"].toInt();

            if (root.contains("trusted_hash")) {
                VkDeviceManager::Instance().SetTrustedHash(root["trusted_hash"].toString());
            }

            Logger::Log(LogLevel::INFO, "VkAuthService: Access token obtained. Exchanging for secret...");
            emit StatusChanged("Получение секретного ключа сессии...");
            ExchangeTokenForSecret(token, userId);
        } else {
            emit AuthError("Неизвестный ответ авторизации от сервера VK.");
        }
    });
}

void VkAuthService::ExchangeTokenForSecret(const std::string& accessToken, int userId) {
    QUrl url(QString(kVkApiBase) + "/method/auth.getExchangeToken");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAuthUserAgent().toUtf8());
    request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(accessToken));
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery body;
    body.addQueryItem("create_common_token", "1");
    body.addQueryItem("create_tier_tokens", "0");
    body.addQueryItem("api_id", VkDeviceManager::kClientId);
    body.addQueryItem("device_id", VkDeviceManager::Instance().GetDeviceId());
    body.addQueryItem("v", VkDeviceManager::kApiVersionAuth);

    QNetworkReply* reply = m_netManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, [this, reply, accessToken, userId]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        std::string secret = "";
        if (root.contains("response")) {
            QJsonObject resp = root["response"].toObject();
            if (resp.contains("users_exchange_tokens")) {
                QJsonArray tokens = resp["users_exchange_tokens"].toArray();
                if (!tokens.isEmpty()) {
                    secret = tokens[0].toObject()["common_token"].toString().toStdString();
                }
            }
        }

        if (!secret.empty()) {
            Logger::Log(LogLevel::INFO, "VkAuthService: Common token (secret) successfully acquired!");
            emit AuthSuccess(accessToken, secret, userId);
        } else {
            Logger::Log(LogLevel::WARNING, "VkAuthService: Failed to get secret from getExchangeToken.");
            // Fallback: emit success with empty secret (or handle error)
            emit AuthSuccess(accessToken, "", userId);
        }
    });
}

void VkAuthService::RefreshToken(const std::string& secret,
                                std::function<void(bool, const std::string&, const std::string&)> callback) {
    if (secret.empty()) {
        callback(false, "", "Secret is empty");
        return;
    }

    QUrl url(QString(kVkApiBase) + "/method/auth.refreshTokens");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAuthUserAgent().toUtf8());
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery body;
    body.addQueryItem("client_id", VkDeviceManager::kClientId);
    body.addQueryItem("client_secret", VkDeviceManager::kClientSecret);
    body.addQueryItem("exchange_tokens", QString::fromStdString(secret));
    body.addQueryItem("active_index", "0");
    body.addQueryItem("scope", "all");
    body.addQueryItem("initiator", "expired_token");
    body.addQueryItem("api_id", VkDeviceManager::kClientId);
    body.addQueryItem("v", VkDeviceManager::kApiVersionAuth);

    QNetworkReply* reply = m_netManager->post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, "", reply->errorString().toStdString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        if (root.contains("response")) {
            QJsonObject resp = root["response"].toObject();
            if (resp.contains("success")) {
                QJsonArray successArr = resp["success"].toArray();
                if (!successArr.isEmpty()) {
                    QJsonObject item = successArr[0].toObject();
                    std::string newToken = item["access_token"].toObject()["token"].toString().toStdString();
                    if (!newToken.empty()) {
                        Logger::Log(LogLevel::INFO, "VkAuthService: Token refreshed successfully via exchange_token!");
                        callback(true, newToken, "");
                        return;
                    }
                }
            }
        }

        std::string err = "Failed to parse refreshed token from response";
        if (root.contains("error")) {
            err = root["error"].toObject()["error_msg"].toString().toStdString();
        }
        callback(false, "", err);
    });
}
