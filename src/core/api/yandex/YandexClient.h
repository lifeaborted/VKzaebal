#pragma once
#include "core/api/BaseApiProvider.h"
#include <QPointer>
#include <QString>
#include <string>

class YandexClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit YandexClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~YandexClient() override;

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void CancelFetchTrackUrl() override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    void SearchAudio(const std::string& query, int count, int offset,
                     std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) override;
    void AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                             std::function<void(bool success, const std::string& error)> callback) override;
    void RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                  std::function<void(bool success, const std::string& error)> callback) override;

    static std::vector<Track> ParseYandexTracks(const QJsonArray& items);

    void SetUserId(const std::string& uid);
    std::string GetUserId() const;

signals:
    void UserIdFetched(const std::string& uid);

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    void EnsureUserId(std::function<void(bool ok)> callback);
    void FetchUserId();
    void FetchLikesIds();
    void FetchTracksMetadata(const QStringList& trackIds, std::function<void()> onComplete = nullptr);

    std::string m_userId;
    QPointer<QNetworkReply> m_currentFetchReply = nullptr;
};