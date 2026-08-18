#pragma once
#include <QObject>
#include <string>
#include <vector>
#include <functional>
#include "models/Track.h"

class IAudioProvider : public QObject {
    Q_OBJECT
public:
    explicit IAudioProvider(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IAudioProvider() = default;

    virtual void SetAccessToken(const std::string& token) = 0;
    virtual void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string& url, bool isNetworkError)> callback) = 0;
    virtual void FetchAllUserAudio(int offset = 0, int count = 200) = 0;

    signals:
        // Общие сигналы для всех сервисов
        void AudioFetched(const std::vector<Track>& tracks);
    void ApiError(const std::string& errorMessage);
    void TokenExpired();
    void FinishedFetching();
};