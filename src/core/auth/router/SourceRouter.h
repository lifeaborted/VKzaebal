#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <memory>
#include <string>
#include <vector>
#include <functional>

class VkClient;
class SpotifyClient;
class SoundCloudClient;
class YandexClient;
class YouTubeClient;

class OAuthManager;
class IAudioProvider;
class QQmlApplicationEngine;

class SourceRouter : public QObject {
    Q_OBJECT
public:
    SourceRouter(const QMap<QString, QString>& envVars, QObject* parent = nullptr);
    ~SourceRouter() override;

    void SwitchSource(const std::string& newSource);
    IAudioProvider* GetCurrentProvider() const { return m_currentProvider; }

    VkClient* GetVkClient() const { return m_vkClient.get(); }
    SpotifyClient* GetSpotifyClient() const { return m_spotifyClient.get(); }
    SoundCloudClient* GetSoundCloudClient() const { return m_soundCloudClient.get(); }
    YandexClient* GetYandexClient() const { return m_yandexClient.get(); }
    YouTubeClient* GetYouTubeClient() const { return m_youtubeClient.get(); }

    OAuthManager* GetAuthManager() const { return m_authManager.get(); }

    void CheckSourceAuthorized(const std::string& source, std::function<void(bool isAuth)> callback) const;
    void FindNextAuthorizedSource(const std::string& excludedSource, std::function<void(const std::string& nextSource)> callback) const;

    signals:
    void SourceChanged(const std::string& newSource);
    void ProviderReady(bool isOnline);
    void AuthUiStateChanged(bool isWaiting);

public slots:
    void Logout(const std::string& service);

private slots:
    void OnVkTokenReceived(const std::string& token);
    void OnSpotifyTokenReceived(const std::string& token);
    void OnSpotifyAuthError(const std::string& err);
    void OnVkTokenExpired();

private:
    void StartVkService();
    void StartSpotifyService();
    void StartSoundCloudService();
    void StartYandexService();
    void StartYouTubeService();

    void StartAuthFlow(const QString& service, const QString& authUrl);

    std::unique_ptr<VkClient> m_vkClient;
    std::unique_ptr<SpotifyClient> m_spotifyClient;
    std::unique_ptr<SoundCloudClient> m_soundCloudClient;
    std::unique_ptr<YandexClient> m_yandexClient;
    std::unique_ptr<YouTubeClient> m_youtubeClient;
    
    std::unique_ptr<OAuthManager> m_authManager;

    QQmlApplicationEngine* m_authEngine = nullptr;
    QString m_currentAuthService;
    QMap<QString, QString> m_envVars;
    IAudioProvider* m_currentProvider = nullptr;
};