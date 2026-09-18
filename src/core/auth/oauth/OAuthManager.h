#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <vector>
#include <functional>

class QSettings;
class OAuthManager : public QObject {
    Q_OBJECT
public:
    explicit OAuthManager(QObject* parent = nullptr);
    ~OAuthManager();

    void GetSavedToken(const QString& service, std::function<void(const std::string&)> callback) const;
    void GetSavedTokens(const QString& service, std::function<void(const std::vector<std::string>&)> callback) const;
    void SaveToken(const std::string& token, const QString& service = "VK") const;
    void ClearSavedToken(const QString& service = "VK") const;

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);
    Q_INVOKABLE void onScTokenIntercepted(const QString& tokenStr);
    Q_INVOKABLE void onYtAuthIntercepted(const QString& cookies);

signals:
    void TokenReceived(const std::string& token);
    void AuthFailed(const std::string& error);
    void AuthCodeReceived(const std::string& code);
    void YtAuthSucceeded(const std::string& cookies);
};