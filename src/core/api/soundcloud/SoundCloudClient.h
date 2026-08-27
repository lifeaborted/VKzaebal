#pragma once
#include "core/api/BaseApiProvider.h"
#include <QString>

class SoundCloudClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit SoundCloudClient(QObject* parent = nullptr);
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

    std::string m_clientId;
    std::string m_userId;
    QString m_nextHref;
};