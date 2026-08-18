#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VkClient : public IAudioProvider {
    Q_OBJECT
public:
    explicit VkClient(QObject* parent = nullptr);
    ~VkClient() override;

    void SetAccessToken(const std::string& token) override;
    void ValidateToken(std::function<void(bool isValid)> callback);

    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool isNetworkError)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

private slots:
    void OnReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
    std::string m_apiVersion = "5.131";
};