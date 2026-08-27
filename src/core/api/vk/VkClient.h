#pragma once
#include "core/api/BaseApiProvider.h"

class VkClient : public BaseApiProvider {
    Q_OBJECT
public:
    explicit VkClient(QObject* parent = nullptr);
    ~VkClient() override;

    void ValidateToken(std::function<void(bool isValid)> callback);
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool isNetworkError)> callback) override;
    void FetchAllUserAudio(int offset = 0, int count = 200) override;

protected:
    bool HandleApiError(const QJsonDocument& json, int httpStatusCode) override;

private:
    std::string m_apiVersion = "5.131";
};