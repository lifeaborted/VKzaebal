#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <memory>


class QSettings;
class OAuthManager : public QObject {
    Q_OBJECT
public:
    explicit OAuthManager(QObject* parent = nullptr);
    ~OAuthManager();

    std::string GetSavedToken(const QString& service = "VK") const;
    void SaveToken(const std::string& token, const QString& service = "VK") const;
    void ClearSavedToken(const QString& service = "VK") const;

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);
    Q_INVOKABLE void onScTokenIntercepted(const QString& tokenStr);

    signals:
        void TokenReceived(const std::string& token);
        void AuthFailed(const std::string& error);
        void AuthCodeReceived(const std::string& code);

private:
    std::unique_ptr<QSettings> m_settings;
};