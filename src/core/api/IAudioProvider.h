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
    [[nodiscard]] virtual std::string GetAccessToken() const { return ""; }
    virtual void FetchTrackUrl(const std::string& trackId, std::function<void(const std::string& url, bool isNetworkError)> callback) = 0;
    virtual void FetchAllUserAudio(int offset = 0, int count = 200) = 0;

    virtual void SearchAudio(const std::string& query, int count, int offset,
                             std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) {
        if (callback) callback({}, "Search not supported for this provider");
    }

    virtual void AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                                     std::function<void(bool success, const std::string& error)> callback) {
        if (callback) callback(false, "Add to favorites not supported for this provider");
    }

    virtual void RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                          std::function<void(bool success, const std::string& error)> callback) {
        if (callback) callback(false, "Remove from favorites not supported for this provider");
    }

    signals:
        // Общие сигналы для всех сервисов
        void AudioFetched(const std::vector<Track>& tracks);
        void ApiError(const std::string& errorMessage);
        void TokenExpired();
        void UserBlocked(const std::string& service, const std::string& reason);
        void FinishedFetching();
};