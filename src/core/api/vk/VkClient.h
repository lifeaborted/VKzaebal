#pragma once
#include "core/api/BaseApiProvider.h"

class VkClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit VkClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~VkClient() override;

    void ValidateToken(std::function<void(bool isValid)> callback);
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool isNetworkError)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    void SearchAudio(const std::string& query, int count, int offset,
                     std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) override;
    void AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                             std::function<void(bool success, const std::string& error)> callback) override;
    void RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                  std::function<void(bool success, const std::string& error)> callback) override;

    static std::vector<Track> ParseVkTracks(const QJsonArray& items);

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    std::string m_apiVersion = "5.131";
    bool m_isValidatingToken = false;
};