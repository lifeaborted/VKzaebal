#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QList>
#include <functional>

class YouTubePoTokenGenerator;

/**
 * @brief Экстрактор аудиопотоков YouTube через InnerTube API.
 * Использует комбинацию современного VisionOS клиента (для получения прямых,
 * не зашифрованных аудиопотоков без обязательного BotGuard) и Web клиента
 * с алгоритмами расшифровки signatureCipher и n-token по спецификации yt-dlp.
 */
class YouTubeExtractor : public QObject {
    Q_OBJECT
public:
    struct CipherOperation {
        enum Type { REVERSE, SLICE, SWAP } type;
        int arg = 0;
    };

    explicit YouTubeExtractor(QNetworkAccessManager* networkManager, YouTubePoTokenGenerator* tokenGen, QObject* parent = nullptr);
    ~YouTubeExtractor() override = default;

    /**
     * @brief Основной асинхронный метод извлечения прямой ссылки на аудиопоток
     * @param videoId Идентификатор видео YouTube
     * @param callback Колбэк с полученным URL аудиопотока и флагом ошибки
     */
    void extractAudioUrl(const QString& videoId, std::function<void(const QString& streamUrl, bool isError)> callback);

    /**
     * @brief C++ функция декодирования зашифрованной подписи (аналог yt-dlp AST парсера)
     */
    QString decryptSignature(const QString& signatureCipher, const QString& baseJs);

    /**
     * @brief C++ функция трансформации n-параметра (защита от троттлинга скорости YouTube)
     */
    QString transformNToken(const QString& rawN, const QString& baseJs);

signals:
    void extractionFinished(const QString& videoId, const QString& streamUrl);
    void extractionFailed(const QString& videoId, const QString& errorMessage);

private:
    void fetchBaseJs(const QString& videoId, std::function<void(const QString& baseJs, const QString& visitorData)> callback);
    void sendVisionOsRequest(const QString& videoId, const QString& visitorData, const QString& baseJs, std::function<void(const QString&, bool)> callback);
    void sendWebRequest(const QString& videoId, const QString& poToken, const QString& visitorData, const QString& baseJs, std::function<void(const QString&, bool)> callback);
    bool parsePlayerResponse(const QJsonObject& root, const QString& videoId, const QString& baseJs, std::function<void(const QString&, bool)> callback);
    bool parseDirectFormats(const QJsonObject& root, const QString& videoId, const QString& baseJs, std::function<void(const QString&, bool)> callback);
    QString extractAudioUrlFromMasterManifest(const QString& manifest);

    // Вспомогательные методы C++ парсинга операций из base.js (AST)
    QList<CipherOperation> parseCipherOperations(const QString& baseJs, QString& outObjName);
    QString executeCipherOperations(QString s, const QList<CipherOperation>& ops);
    QString evaluateSignatureInJs(const QString& s, const QString& baseJs);

    QNetworkAccessManager* m_manager = nullptr;
    YouTubePoTokenGenerator* m_tokenGen = nullptr;

    QString m_cachedBaseJs;
    QString m_baseJsUrl;
    QString m_cachedVisitorData;
};
