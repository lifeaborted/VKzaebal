#pragma once
#include "core/api/BaseApiProvider.h"
#include <string>
#include <functional>
#include <memory>

class YouTubePoTokenGenerator;
class YouTubeExtractor;

/**
 * @brief Провайдер API для сервиса YouTube в архитектуре плеера.
 * Делегирует генерацию токенов и распаковку аудиопотоков специализированным классам.
 */
class YouTubeClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit YouTubeClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~YouTubeClient() override;

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    YouTubePoTokenGenerator* GetTokenGenerator() const { return m_tokenGenerator.get(); }
    YouTubeExtractor* GetExtractor() const { return m_extractor.get(); }

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    void FetchLikedMusic(int offset, int count, const QString& continuationToken = "");
    std::vector<Track> parseTracksFromBrowseResponse(const QJsonObject& root);
    QString extractContinuationToken(const QJsonObject& root);

    static QString extractCookieValue(const QString& cookies, const QString& key);
    static QString generateSapisidHash(const QString& cookies, const QString& origin);

    std::unique_ptr<YouTubePoTokenGenerator> m_tokenGenerator;
    std::unique_ptr<YouTubeExtractor> m_extractor;

    QString m_continuationToken;
    int m_totalFetched = 0;
};