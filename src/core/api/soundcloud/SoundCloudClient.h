#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <string>

class SoundCloudClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit SoundCloudClient(QObject* parent = nullptr);
    ~SoundCloudClient() override;

    void InitializeWithToken();

    void SetAccessToken(const std::string& token) override;
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

private:
    void FetchClientId();
    void ExtractClientIdFromJs(const QString& jsUrl);
    void FetchMe();

    QNetworkAccessManager* m_manager;
    std::string m_clientId;
    std::string m_accessToken;
    std::string m_userId;
    QString m_nextHref;
};