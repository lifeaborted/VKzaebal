#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <vector>
#include <functional>

class QSettings;
class QNetworkAccessManager;

class OAuthManager : public QObject {
    Q_OBJECT
public:
    explicit OAuthManager(QObject* parent = nullptr, QNetworkAccessManager* netManager = nullptr);
    ~OAuthManager();

    void GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const;
    void GetSavedTokens(const QString& service, std::function<void(const std::vector<std::string>&)> callback) const;
    void SaveToken(const std::string& token, const QString& service = "VK") const;
    void ClearSavedToken(const QString& service = "VK") const;

    void SaveCookies(const std::string& cookies, const QString& service = "VK") const;
    void GetSavedCookies(const QString& service, std::function<void(const std::string&)> callback) const;
    void ClearSavedCookies(const QString& service = "VK") const;

    void SaveUserId(const std::string& uid, const QString& service = "Yandex") const;
    void GetSavedUserId(const QString& service, std::function<void(const std::string&)> callback) const;
    void ClearSavedUserId(const QString& service = "Yandex") const;

    void RefreshVkTokenSilently(const std::string& cookies,
                                const QString& authUrl,
                                std::function<void(const std::string& newToken, bool success)> callback);

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);
    Q_INVOKABLE void onScTokenIntercepted(const QString& tokenStr);
    Q_INVOKABLE void onYtAuthIntercepted(const QString& cookies);

signals:
    void TokenReceived(const std::string& token);
    void AuthFailed(const std::string& error);
    void AuthCodeReceived(const std::string& code);
    void YtAuthSucceeded(const std::string& cookies);

private:
    QNetworkAccessManager* m_netManager = nullptr;
    bool m_ownsNetManager = false;
};