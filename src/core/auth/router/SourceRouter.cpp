#include "SourceRouter.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/auth/oauth/OAuthManager.h"
#include "utils/logger/Logger.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>
#include <iostream>

#include "core/api/soundcloud/SoundCloudClient.h"

SourceRouter::SourceRouter(const QMap<QString, QString>& envVars, QObject* parent)
    : QObject(parent), m_envVars(envVars) {
    m_vkClient = std::make_unique<VkClient>();
    m_spotifyClient = std::make_unique<SpotifyClient>();
    m_soundCloudClient = std::make_unique<SoundCloudClient>();
    m_authManager = std::make_unique<OAuthManager>();

    connect(m_authManager.get(), &OAuthManager::TokenReceived, this, [&](const std::string& token) {
            if (m_currentAuthService == "VK") OnVkTokenReceived(token);
            else if (m_currentAuthService == "Spotify") OnSpotifyTokenReceived(token);
            else if (m_currentAuthService == "SoundCloud") {
                if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
                m_authManager->SaveToken(token, "SoundCloud");
                emit AuthUiStateChanged(false);
                std::cout << "\n[УСПЕХ] Авторизация SoundCloud пройдена! Токен перехвачен.\n> ";
                std::cout.flush();

                m_soundCloudClient->SetAccessToken(token);
                m_soundCloudClient->InitializeWithToken();
                emit ProviderReady(true);
            }
        });

    connect(m_authManager.get(), &OAuthManager::AuthCodeReceived, this, [&](const std::string& code) {
            static std::string lastCode = "";
            if (code == lastCode) return;
            lastCode = code;

            if (m_currentAuthService == "Spotify") {
                if (m_authEngine) {
                    m_authEngine->deleteLater();
                    m_authEngine = nullptr;
                    emit AuthUiStateChanged(false);
                    std::cout << "\n[Инфо] Код перехвачен. Закрываем окно авторизации...\n> ";
                    std::cout.flush();
                }

                Logger::Log(LogLevel::INFO, "SourceRouter: Exchanging Spotify code for token...");
                m_spotifyClient->ExchangeCodeForToken(code);
            }
        });

    connect(m_spotifyClient.get(), &SpotifyClient::TokenReceived, this, &SourceRouter::OnSpotifyTokenReceived);
    connect(m_spotifyClient.get(), &SpotifyClient::AuthError, this, &SourceRouter::OnSpotifyAuthError);
    connect(m_vkClient.get(), &VkClient::TokenExpired, this, &SourceRouter::OnVkTokenExpired);
}

SourceRouter::~SourceRouter() {
    if (m_authEngine) {
        m_authEngine->deleteLater();
    }
}

void SourceRouter::StartAuthFlow(const QString& service, const QString& authUrl) {
    m_currentAuthService = service;
    Logger::Log(LogLevel::INFO, "SourceRouter: Starting auth flow via QML for " + service.toStdString() + "...");
    emit AuthUiStateChanged(true);

    std::cout << "\n=== Авторизация " << service.toStdString() << " ===\n";
    std::cout << "Откроется окно браузера. Войдите в аккаунт, токен перехватится автоматически.\n> ";
    std::cout.flush();

    if (!m_authEngine) {
        m_authEngine = new QQmlApplicationEngine();
        m_authEngine->rootContext()->setContextProperty("cppAuthManager", m_authManager.get());
        m_authEngine->rootContext()->setContextProperty("cppAuthUrl", authUrl);
        m_authEngine->load(QUrl(QStringLiteral("qrc:/core/auth/auth.qml")));

        if (m_authEngine->rootObjects().isEmpty()) {
            Logger::Log(LogLevel::ERROR, "SourceRouter: Failed to load auth.qml!");
        } else {
            QWindow* rootWindow = qobject_cast<QWindow*>(m_authEngine->rootObjects().first());
            if (rootWindow) {
                connect(rootWindow, &QWindow::visibleChanged, this, [this](bool visible) {
                    if (!visible && m_authEngine) {
                        m_authEngine->deleteLater();
                        m_authEngine = nullptr;
                        emit AuthUiStateChanged(false);
                        std::cout << "\n[Инфо] Окно авторизации закрыто.\n> ";
                        std::cout.flush();
                    }
                });
            }
        }
    }
}

void SourceRouter::OnVkTokenReceived(const std::string& token) {
    if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
    m_authManager->SaveToken(token, "VK");
    emit AuthUiStateChanged(false);
    std::cout << "\n[УСПЕХ] Авторизация VK пройдена!\n> ";
    std::cout.flush();

    m_vkClient->SetAccessToken(token);
    emit ProviderReady(true);
    m_vkClient->FetchAllUserAudio(0, 200);
}

void SourceRouter::OnSpotifyTokenReceived(const std::string& token) {
    m_authManager->SaveToken(token, "Spotify");
    emit AuthUiStateChanged(false);
    std::cout << "\n[УСПЕХ] Авторизация Spotify пройдена!\n> ";
    std::cout.flush();

    m_spotifyClient->SetAccessToken(token);
    emit ProviderReady(true);
    m_spotifyClient->FetchAllUserAudio(0, 50);
}

void SourceRouter::OnSpotifyAuthError(const std::string& err) {
    std::cout << "\n[ОШИБКА] Не удалось получить токен Spotify: " << err << "\n> ";
    std::cout.flush();
    emit AuthUiStateChanged(false);
}

void SourceRouter::OnVkTokenExpired() {
    Logger::Log(LogLevel::WARNING, "SourceRouter: Token VK expired.");
    std::cout << "\n[ВНИМАНИЕ] Токен ВК устарел.\n";
    m_authManager->ClearSavedToken("VK");
    m_vkClient->SetAccessToken("");
    StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
}

void SourceRouter::StartVkService() {
    std::string savedToken = m_authManager->GetSavedToken("VK");
    if (savedToken.empty()) {
        StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
    } else {
        m_vkClient->SetAccessToken(savedToken);
        m_vkClient->ValidateToken([this](bool isValid) {
            if (isValid) {
                emit AuthUiStateChanged(false);
                emit ProviderReady(true);
                m_vkClient->FetchAllUserAudio(0, 200);
            } else {
                m_authManager->ClearSavedToken("VK");
                m_vkClient->SetAccessToken("");
                StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
            }
        });
    }
}

void SourceRouter::StartSpotifyService() {
    QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
    QString clientId = m_envVars.value("SPOTIFY_CLIENT_ID", "");
    std::string savedToken = m_authManager->GetSavedToken("Spotify");

    // --- РЕЖИМ 1: Обход через sp_dc ---
    if (!spDc.isEmpty()) {
        if (savedToken.empty()) {
            std::cout << "\n[Spotify] Получение Web Access Token через sp_dc...\n"; std::cout.flush();
            m_spotifyClient->AuthWithSpDc(spDc);
        } else {
            m_spotifyClient->SetAccessToken(savedToken);
            std::cout << "Проверка сохраненного токена Spotify (sp_dc)...\n"; std::cout.flush();

            m_spotifyClient->ValidateToken([this, spDc](bool isValid) {
                if (isValid) {
                    emit AuthUiStateChanged(false);
                    std::cout << "\n[УСПЕХ] Синхронизация треков Spotify...\n> "; std::cout.flush();
                    emit ProviderReady(true);
                    m_spotifyClient->FetchAllUserAudio(0, 50);
                } else {
                    std::cout << "\n[ВНИМАНИЕ] Токен Spotify устарел. Тихое обновление...\n"; std::cout.flush();
                    m_authManager->ClearSavedToken("Spotify");
                    m_spotifyClient->SetAccessToken("");
                    m_spotifyClient->AuthWithSpDc(spDc);
                }
            });
        }
    }
    // --- РЕЖИМ 2: Официальный PKCE ---
    else if (!clientId.isEmpty()) {
        if (savedToken.empty()) {
            std::string authUrl = m_spotifyClient->StartAuthPkce(clientId);
            StartAuthFlow("Spotify", QString::fromStdString(authUrl));
        } else {
            m_spotifyClient->SetAccessToken(savedToken);
            std::cout << "Проверка сохраненного токена Spotify (PKCE)...\n"; std::cout.flush();

            m_spotifyClient->ValidateToken([this, clientId](bool isValid) {
                if (isValid) {
                    emit AuthUiStateChanged(false);
                    std::cout << "\n[УСПЕХ] Синхронизация треков Spotify...\n> "; std::cout.flush();
                    emit ProviderReady(true);
                    m_spotifyClient->FetchAllUserAudio(0, 50);
                } else {
                    std::cout << "\n[ВНИМАНИЕ] Токен Spotify устарел. Открытие окна авторизации...\n"; std::cout.flush();
                    m_authManager->ClearSavedToken("Spotify");
                    m_spotifyClient->SetAccessToken("");
                    std::string authUrl = m_spotifyClient->StartAuthPkce(clientId);
                    StartAuthFlow("Spotify", QString::fromStdString(authUrl));
                }
            });
        }
    }
    // --- ОШИБКА КОНФИГУРАЦИИ ---
    else {
        emit AuthUiStateChanged(false);
        std::cout << "\n[ОШИБКА] В .env не задан ни SPOTIFY_SP_DC, ни SPOTIFY_CLIENT_ID!\n> ";
        std::cout.flush();
    }
}

void SourceRouter::SwitchSource(const std::string& newSource) {
    Logger::Log(LogLevel::INFO, "SourceRouter: Switching audio source to " + newSource);

    if (newSource == "VK") {
        m_currentProvider = m_vkClient.get();
    } else if (newSource == "Spotify") {
        m_currentProvider = m_spotifyClient.get();
    } else if (newSource == "SoundCloud") {
        m_currentProvider = m_soundCloudClient.get();
    } else if (newSource == "Offline") {
        m_currentProvider = nullptr;
    }

    emit SourceChanged(newSource);

    if (newSource == "VK") {
        StartVkService();
    } else if (newSource == "Spotify") {
        StartSpotifyService();
    } else if (newSource == "SoundCloud") {
        StartSoundCloudService();
    } else if (newSource == "Offline") {
        emit AuthUiStateChanged(false);
        emit ProviderReady(false);
    }
}

void SourceRouter::StartSoundCloudService() {
    std::string savedToken = m_authManager->GetSavedToken("SoundCloud");

    if (savedToken.empty()) {
        std::cout << "\n[SoundCloud] Токен не найден. Открываем окно авторизации...\n";
        std::cout.flush();
        // Запускаем окно на странице входа
        StartAuthFlow("SoundCloud", "https://soundcloud.com/signin");
    } else {
        std::cout << "\n[SoundCloud] Инициализация по сохраненному токену...\n";
        std::cout.flush();
        m_soundCloudClient->SetAccessToken(savedToken);
        m_soundCloudClient->InitializeWithToken();
        emit ProviderReady(true);
    }
}