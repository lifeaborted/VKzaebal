#pragma once
#include "core/api/IAudioProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <functional>
#include <string>

class BaseApiProvider : public IAudioProvider {
    Q_OBJECT
public:
    explicit BaseApiProvider(QObject* parent = nullptr);
    ~BaseApiProvider() override;

    void SetAccessToken(const std::string& token) override;

protected:
    void SendJsonRequest(QNetworkRequest request, 
                         std::function<void(const QJsonDocument&)> onSuccess, 
                         std::function<void(const std::string&)> onFail = nullptr,
                         const QByteArray& postData = QByteArray());

    virtual bool HandleApiError(const QJsonDocument& json, int httpStatusCode) = 0;

    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
};