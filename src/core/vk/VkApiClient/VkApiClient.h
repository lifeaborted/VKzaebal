#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <vector>
#include <string>
#include <functional>
#include "core/vk/Track.h"

class VkApiClient : public QObject {
    Q_OBJECT
public:
    explicit VkApiClient(QObject* parent = nullptr);
    ~VkApiClient();

    // Установка токена доступа
    void SetAccessToken(const std::string& token);

    // Валидация токена
    void ValidateToken(std::function<void(bool isValid)> callback); //

    // запрос свежей ссылки на трек
    void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string& freshUrl)> callback);

    // Запрос списка аудиозаписей пользователя
    void FetchUserAudio(long long ownerId = 0, int count = 100);

    void FetchAllUserAudio(int offset = 0, int count = 200); // загрузчик треков

    signals:
        void AudioFetched(const std::vector<Track>& tracks);
        void ApiError(const std::string& errorMessage);
        void TokenExpired();
private slots:
    void OnReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_manager;
    std::string m_accessToken;
    std::string m_apiVersion = "5.131"; // Актуальная версия API
};