#pragma once
#include "core/api/BaseApiProvider.h"
#include <QString>

class SpotifyClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit SpotifyClient(QObject* parent = nullptr);
    ~SpotifyClient() override;
    void AuthWithSpDc(const QString& spDcCookie);
    std::string StartAuthPkce(const QString& clientId);
    void ExchangeCodeForToken(const std::string& code);

    void ValidateToken(std::function<void(bool)> callback);
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

    signals:
        void TokenReceived(const std::string& token);
    void AuthError(const std::string& errorString);

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    QString m_clientId;
    QString m_codeVerifier;
};