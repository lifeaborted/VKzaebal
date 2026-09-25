#pragma once
#include "core/api/BaseApiProvider.h"
#include <vector>
#include <utility>

class VkClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit VkClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~VkClient() override;

    void SetSecret(const std::string& secret);
    [[nodiscard]] std::string GetSecret() const { return m_secret; }

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

    // Calculates MD5 signature for VK Android API requests: md5("/method/" + method + "?" + RAW_PARAMS + secret)
    QString CalculateSig(const std::string& method, const std::vector<std::pair<QString, QString>>& params) const;

    // Builds a standard signed POST request targeting https://api.vk.ru/method/{method}
    QNetworkRequest BuildSignedPostRequest(const std::string& method,
                                           const std::vector<std::pair<QString, QString>>& params,
                                           QByteArray& outBody) const;

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    std::string m_secret;
    std::string m_apiVersion = "5.87";
    bool m_isValidatingToken = false;
};