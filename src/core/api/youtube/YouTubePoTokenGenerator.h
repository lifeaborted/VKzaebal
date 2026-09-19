#pragma once
#include <QObject>
#include <QString>
#include <QJSEngine>
#include <functional>

/**
 * @brief Генератор Proof of Origin (po_token) на базе QJSEngine с полифил-окружением.
 * Создает минимальный mock DOM/BOM (window, document, navigator, location, performance,
 * crypto, canvas, storage), предотвращая ошибки 'undefined' при выполнении BotGuard JS.
 */
class YouTubePoTokenGenerator : public QObject {
    Q_OBJECT
public:
    explicit YouTubePoTokenGenerator(QObject* parent = nullptr);
    ~YouTubePoTokenGenerator() override = default;

    /**
     * @brief Асинхронно генерирует po_token и visitorData
     * @param jsCode Исполняемый BotGuard JS скрипт (если пуст, генерируется валидный Web session PoToken)
     * @param callback Колбэк с сигнатурой void(QString poToken, QString visitorData)
     */
    void generateToken(const QString& jsCode, std::function<void(QString poToken, QString visitorData)> callback = nullptr);

signals:
    void tokenGenerated(const QString& poToken, const QString& visitorData);
    void tokenError(const QString& errorMessage);

private:
    static void setupPolyfillEnvironment(QJSEngine& engine);
    static QString generateWebPoTokenFallback();
};
