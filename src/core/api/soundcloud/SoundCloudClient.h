#pragma once
#include "core/api/BaseApiProvider.h"
#include <QString>
#include <vector>
#include <utility>
#include <functional>

class SoundCloudClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit SoundCloudClient(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~SoundCloudClient() override;

    void InitializeWithToken();
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    void FetchClientId();
    void ExtractClientIdFromJs(const QString& jsUrl);
    void FetchMe();
    void FailPendingRequests();

    std::string m_clientId;
    std::string m_userId;
    QString m_nextHref;
    bool m_isFetchingClientId = false;
    std::vector<std::pair<std::string, std::function<void(const std::string&, bool)>>> m_pendingTrackRequests;
};