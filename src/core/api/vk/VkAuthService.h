#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <functional>
#include <string>

class QNetworkAccessManager;
class QNetworkReply;

class VkAuthService : public QObject {
    Q_OBJECT
public:
    explicit VkAuthService(QObject* parent = nullptr, QNetworkAccessManager* netManager = nullptr);
    ~VkAuthService() override;

    // Starts the native VK Android login flow for a given phone or email (optional password upfront)
    void StartLogin(const QString& login, const QString& password = "");

    // Provide password when PasswordRequired is emitted
    void SubmitPassword(const QString& password);

    // Provide 2FA / SMS / push verification code when CodeRequired is emitted
    void SubmitCode(const QString& code);

    // Provide captcha response when CaptchaRequired is emitted
    void SubmitCaptcha(const QString& captchaSid, const QString& captchaKey, const QString& successToken = "");
    void SubmitCaptchaKey(const QString& captchaKey);
    QString GetLastCaptchaSid() const { return m_lastCaptchaSid; }

    // Silent token refresh using secret (exchange_token)
    void RefreshToken(const std::string& secret, std::function<void(bool success, const std::string& newToken, const std::string& error)> callback);

signals:
    void CaptchaRequired(const QString& redirectUri, const QString& captchaImg, const QString& captchaSid);
    void PasswordRequired(const QString& sid);
    void CodeRequired(const QString& verificationMethod, const QString& sid, const QString& phoneMask);
    void AuthSuccess(const std::string& accessToken, const std::string& secret, int userId);
    void AuthError(const QString& errorMsg);
    void StatusChanged(const QString& status);

private:
    void FetchAnonymToken(std::function<void(bool, const QString&)> callback);
    void ValidateAccount();
    void RequestToken();
    void ExchangeTokenForSecret(const std::string& accessToken, int userId);

    QNetworkAccessManager* m_netManager = nullptr;
    bool m_ownsNetManager = false;

    QString m_login;
    QString m_savedPassword;
    QString m_savedCode;
    QString m_anonymToken;
    qint64 m_anonymTokenExpiry = 0;
    QString m_sid;
    QString m_validationSid;
    QString m_flowName;
    QString m_verificationMethod;
    bool m_needPassword = false;
    QString m_lastCaptchaSid;
    QString m_lastCaptchaKey;
    QString m_lastSuccessToken;
};
