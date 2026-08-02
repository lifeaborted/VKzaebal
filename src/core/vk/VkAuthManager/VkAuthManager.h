#pragma once
#include <QObject>
#include <QString>
#include <string>

class VkAuthManager : public QObject {
    Q_OBJECT
public:
    explicit VkAuthManager(QObject* parent = nullptr);
    ~VkAuthManager();

    std::string GetSavedToken() const;
    void SaveToken(const std::string& token) const;

    // Обычный C++ метод для парсинга
    void onUrlIntercepted(const QString& urlStr);

    signals:
        void TokenReceived(const std::string& token);
    void AuthFailed(const std::string& error);
};