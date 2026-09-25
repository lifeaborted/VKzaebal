#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QMap>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "models/Track.h"

class VkClient;
class SpotifyClient;
class SoundCloudClient;
class YandexClient;
class YouTubeClient;
class QNetworkAccessManager;

class VkAuthService;
class OAuthManager;
class IAudioProvider;
class QQmlApplicationEngine;
class QTimer;

class SourceRouter : public QObject {
    Q_OBJECT
public:
    explicit SourceRouter(const QMap<QString, QString>& envVars,
                          QNetworkAccessManager* networkManager = nullptr,
                          QObject* parent = nullptr);
    ~SourceRouter() override;

    void SwitchSource(const std::string& newSource);
    IAudioProvider* GetCurrentProvider() const { return m_currentProvider; }
    IAudioProvider* GetProvider(const std::string& sourceName) const;
    IAudioProvider* GetOrCreateProvider(const std::string& sourceName);

    VkClient* GetVkClient() const;
    SpotifyClient* GetSpotifyClient() const;
    SoundCloudClient* GetSoundCloudClient() const;
    YandexClient* GetYandexClient() const;
    YouTubeClient* GetYouTubeClient() const;

    OAuthManager* GetAuthManager() const { return m_authManager.get(); }
    VkAuthService* GetVkAuthService() const { return m_vkAuthService.get(); }

    void CheckSourceAuthorized(const std::string& source, std::function<void(bool isAuth)> callback) const;
    void FindNextAuthorizedSource(const std::string& excludedSource, std::function<void(const std::string& nextSource)> callback) const;
    void EnsureAllProvidersInitialized();
    void PreinitializeVkClient();
    void PreinitializeSpotifyClient();
    void PreinitializeSoundCloudClient();
    void PreinitializeYandexClient();
    void PreinitializeYouTubeClient();

    void Search(const std::string& source, const std::string& query, int count, int offset,
                std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback);
    void AddTrackToFavorites(const Track& track, std::function<void(bool success, const std::string& error)> callback);
    void RemoveTrackFromFavorites(const Track& track, std::function<void(bool success, const std::string& error)> callback);

signals:
    void SourceChanged(const std::string& newSource);
    void ProviderReady(bool isOnline);
    void AuthUiStateChanged(bool isWaiting);
    void StatusMessageRequested(const std::string& msg);
    void AudioFetched(const std::vector<Track>& tracks);
    void FinishedFetching();

public slots:
    void Logout(const std::string& service);
    void OnVkTokenExpired();

private slots:
    void OnSpotifyTokenReceived(const std::string& token);
    void OnSpotifyAuthError(const std::string& err);

private:
    void EmitStatus(const std::string& msg);

    void StartVkService();
    void StartSpotifyService();
    void StartSoundCloudService();
    void StartYandexService();
    void StartYouTubeService();

    void StartAuthFlow(const QString& service, const QString& authUrl, bool forceVisible = false);
    void CheckNextCandidate(const std::shared_ptr<const std::vector<std::string>>& candidates,
                            size_t index,
                            std::function<void(const std::string& nextSource)> callback) const;

    QNetworkAccessManager* m_networkManager = nullptr;
    std::unordered_map<std::string, std::unique_ptr<IAudioProvider>> m_providers;

    std::unique_ptr<OAuthManager> m_authManager;
    std::unique_ptr<VkAuthService> m_vkAuthService;

    QQmlApplicationEngine* m_authEngine = nullptr;
    QTimer* m_silentAuthTimer = nullptr;
    QString m_currentAuthService;
    QMap<QString, QString> m_envVars;
    IAudioProvider* m_currentProvider = nullptr;

    bool m_isAuthFlowActive = false;
    qint64 m_lastAuthAttemptMs = 0;
};