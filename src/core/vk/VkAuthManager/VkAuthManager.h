#pragma once
#include <QObject>
#include <QString>
#include <string>
#include <QQuickView>

class QQuickWidget;

class VkAuthManager : public QObject {
    Q_OBJECT
public:
    explicit VkAuthManager(QObject* parent = nullptr);
    ~VkAuthManager();

    std::string GetSavedToken() const;
    void SaveToken(const std::string& token) const;

    // передаем ID приложения
    void Authenticate(int appId);

public slots:
    // Слот для перехвата URL из QML-браузера
    void onUrlIntercepted(const QString& urlString);

    signals:
        void TokenReceived(const std::string& token);
    void AuthFailed(const std::string& error);

private:
    QQuickView* m_view = nullptr;
};