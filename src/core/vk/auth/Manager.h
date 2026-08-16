#pragma once
#include <QObject>
#include <QString>
#include <string>

class Manager : public QObject {
    Q_OBJECT
public:
    explicit Manager(QObject* parent = nullptr);
    ~Manager();

    std::string GetSavedToken() const;
    void SaveToken(const std::string& token) const;
    void ClearSavedToken() const;

    Q_INVOKABLE void onUrlIntercepted(const QString& urlStr);

    signals:
        void TokenReceived(const std::string& token);
    void AuthFailed(const std::string& error);
};