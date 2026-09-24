#pragma once
#include "core/api/BaseApiProvider.h"
#include <QString>
#include <vector>
#include <utility>
#include <functional>

#include <unordered_map>

class SoundCloudClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit SoundCloudClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~SoundCloudClient() override;

    void InitializeWithToken();
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    void SearchAudio(const std::string& query, int count, int offset,
                     std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) override;
    void AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                             std::function<void(bool success, const std::string& error)> callback) override;
    void RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                  std::function<void(bool success, const std::string& error)> callback) override;

    std::vector<Track> ParseSoundCloudTracks(const QJsonArray& collection);

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    struct TranscodingInfo {
        QString transUrl;
        QString trackAuth;
    };

    void FetchClientId();
    void ExtractClientIdFromJs(const QString& jsUrl);
    void FetchMe();
    void FailPendingRequests();
    void RequestCdnUrl(const QString& transUrl, const QString& trackAuth, std::function<void(const std::string&, bool)> callback);

    std::string m_clientId;
    std::string m_userId;
    QString m_nextHref;
    bool m_isFetchingClientId = false;
    std::vector<std::pair<std::string, std::function<void(const std::string&, bool)>>> m_pendingTrackRequests;
    std::unordered_map<std::string, TranscodingInfo> m_trackTranscodings;
};