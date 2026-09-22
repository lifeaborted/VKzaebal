#pragma once
#include "core/api/BaseApiProvider.h"
#include <QString>
#include <string>

class YandexClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit YandexClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~YandexClient() override;

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    void SetUserId(const std::string& uid);
    std::string GetUserId() const;

signals:
    void UserIdFetched(const std::string& uid);

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    void FetchUserId();
    void FetchLikesIds(int offset, int count);
    void FetchTracksMetadata(const QStringList& trackIds);

    std::string m_userId;
};