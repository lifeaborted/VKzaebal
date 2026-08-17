#pragma once
#include <QObject>
#include <QString>
#include <string>

class Manager : public QObject {
    Q_OBJECT
public:
    explicit Manager(QObject* parent = nullptr);
    ~Manager();

    std::string GetSavedToken(const QString& service = "VK") const;
    void SaveToken(const std::string& token, const QString& service = "VK") const;
    void ClearSavedToken(const QString& service = "VK") const;

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);

    signals:
        void TokenReceived(const std::string& token);
        void AuthFailed(const std::string& error);
        void AuthCodeReceived(const std::string& code);
};