#include "SourceRouter.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/api/yandex/YandexClient.h"
#include "core/api/youtube/YouTubeClient.h"
#include "core/auth/oauth/OAuthManager.h"
#include "core/auth/oauth/WebViewCookieReader.h"
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
    m_yandexClient = std::make_unique<YandexClient>();
    m_youtubeClient = std::make_unique<YouTubeClient>();

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

            else if (m_currentAuthService == "Yandex") {
                if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
                m_authManager->SaveToken(token, "Yandex");
                emit AuthUiStateChanged(false);
                std::cout << "\n[УСПЕХ] Авторизация Yandex пройдена!\n> ";
                std::cout.flush();

                m_yandexClient->SetAccessToken(token);
                emit ProviderReady(true);
                m_yandexClient->FetchAllUserAudio(0, 200);
            }
        });

    // Перехват успеха авторизации YouTube
    connect(m_authManager.get(), &OAuthManager::YtAuthSucceeded, this, [this](const std::string& cookies) {
        if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }

        emit AuthUiStateChanged(false);
        std::cout << "\n[УСПЕХ] Авторизация YouTube Music пройдена! Синхронизация избранных треков...\n> ";
        std::cout.flush();

        m_youtubeClient->SetAccessToken(cookies);
        emit ProviderReady(true);
        m_youtubeClient->FetchAllUserAudio(0, 100);
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

    // НОВЫЙ БЛОК: Если YouTube откидывает сессию, заставляем пользователя логиниться заново
    connect(m_youtubeClient.get(), &YouTubeClient::TokenExpired, this, [this]() {
        Logger::Log(LogLevel::WARNING, "SourceRouter: YouTube session expired or BotGuard rejected.");
        std::cout << "\n[ВНИМАНИЕ] Сессия YouTube Music устарела или отклонена.\n";
        std::cout.flush();
        m_authManager->ClearSavedToken("YouTube");
        m_youtubeClient->SetAccessToken("");
        StartAuthFlow("YouTube", "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F");
    });
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
    Logger::Log(LogLevel::WARNING, "SourceRouter: Token VK expired or rejected (possibly IP changed). Testing token pool...");
    m_authManager->GetSavedTokens("VK", [this](const std::vector<std::string>& savedTokens) {
        if (savedTokens.empty()) {
            m_vkClient->SetAccessToken("");
            StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
        } else {
            TryValidateVkTokens(savedTokens, 0);
        }
    });
}

void SourceRouter::StartVkService() {
    m_authManager->GetSavedTokens("VK", [this](const std::vector<std::string>& savedTokens) {
        if (savedTokens.empty()) {
            std::cout << "\n[VK] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
        } else {
            std::cout << "\n[VK] Проверка сохраненных токенов (" << savedTokens.size() << " в пуле)...\n";
            std::cout.flush();
            TryValidateVkTokens(savedTokens, 0);
        }
    });
}

void SourceRouter::TryValidateVkTokens(const std::vector<std::string>& tokens, size_t index) {
    if (index >= tokens.size()) {
        std::cout << "\n[VK] Ни один токен из пула не подошел под текущий IP. Получение токена...\n";
        std::cout.flush();
        m_vkClient->SetAccessToken("");
        StartAuthFlow("VK", "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=408861919&response_type=token&v=5.131");
        return;
    }

    const std::string& currentToken = tokens[index];
    m_vkClient->SetAccessToken(currentToken);
    m_vkClient->ValidateToken([this, tokens, index, currentToken](bool isValid) {
        if (isValid) {
            Logger::Log(LogLevel::INFO, "SourceRouter: VK token from pool (index " + std::to_string(index) + ") is valid for current IP.");
            m_authManager->SaveToken(currentToken, "VK");
            emit AuthUiStateChanged(false);
            emit ProviderReady(true);
            m_vkClient->FetchAllUserAudio(0, 200);
        } else {
            TryValidateVkTokens(tokens, index + 1);
        }
    });
}

void SourceRouter::StartSoundCloudService() {
    m_authManager->GetSavedToken("SoundCloud", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            std::cout << "\n[SoundCloud] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("SoundCloud", "https://soundcloud.com/signin");
        } else {
            std::cout << "\n[SoundCloud] Инициализация по сохраненному токену...\n";
            std::cout.flush();
            m_soundCloudClient->SetAccessToken(savedToken);
            m_soundCloudClient->InitializeWithToken();
            emit ProviderReady(true);
        }
    });
}

void SourceRouter::StartSpotifyService() {
    QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
    QString clientId = m_envVars.value("SPOTIFY_CLIENT_ID", "");

    m_authManager->GetSavedToken("Spotify", [this, spDc, clientId](const std::string& savedToken) {

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
        else {
            emit AuthUiStateChanged(false);
            std::cout << "\n[ОШИБКА] В .env не задан ни SPOTIFY_SP_DC, ни SPOTIFY_CLIENT_ID!\n> ";
            std::cout.flush();
        }
    });
}

void SourceRouter::StartYandexService() {
    m_authManager->GetSavedToken("Yandex", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            std::cout << "\n[Yandex] Токен не найден. Открываем окно авторизации...\n";
            std::cout.flush();
            StartAuthFlow("Yandex", "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d");
        } else {
            std::cout << "\n[Yandex] Инициализация по сохраненному токену...\n";
            std::cout.flush();
            m_yandexClient->SetAccessToken(savedToken);
            emit ProviderReady(true);
            m_yandexClient->FetchAllUserAudio(0, 200);
        }
    });
}

void SourceRouter::StartYouTubeService() {
    QString envCookie = m_envVars.value("YOUTUBE_COOKIE", "");

    m_authManager->GetSavedToken("YouTube", [this, envCookie](const std::string& savedToken) {
        std::string effectiveToken = !envCookie.isEmpty() ? envCookie.toStdString() : savedToken;

        // Если в Keychain нет LOGIN_INFO, пробуем прочитать активную сессию из WebView2
        if (effectiveToken.find("LOGIN_INFO=") == std::string::npos) {
            std::string fullCookies = WebViewCookieReader::GetFullYouTubeCookies();
            if (!fullCookies.empty() && fullCookies.find("LOGIN_INFO=") != std::string::npos) {
                Logger::Log(LogLevel::INFO, "SourceRouter: Retrieved active YouTube session from WebView2 storage.");
                effectiveToken = fullCookies;
                m_authManager->SaveToken(effectiveToken, "YouTube");
            }
        }

        bool hasValidCookies = (effectiveToken.find("LOGIN_INFO=") != std::string::npos || !envCookie.isEmpty()) &&
                               (effectiveToken.find("SAPISID=") != std::string::npos || effectiveToken.find("__Secure-1PAPISID=") != std::string::npos);

        if (effectiveToken.empty() || !hasValidCookies) {
            if (!effectiveToken.empty()) {
                m_authManager->ClearSavedToken("YouTube");
            }
            std::cout << "\n[YouTube] Требуется авторизация для загрузки вашей медиатеки...\n";
            std::cout.flush();
            StartAuthFlow("YouTube", "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F");
        } else {
            std::cout << "\n[YouTube] Сессия найдена. Загрузка избранных треков...\n";
            std::cout.flush();
            m_youtubeClient->SetAccessToken(effectiveToken);
            emit AuthUiStateChanged(false);
            emit ProviderReady(true);
            m_youtubeClient->FetchAllUserAudio(0, 100);
        }
    });
}

void SourceRouter::SwitchSource(const std::string& newSource) {
    Logger::Log(LogLevel::INFO, "SourceRouter: Switching audio source to " + newSource);

    if (newSource == "VK") {
        m_currentProvider = m_vkClient.get();
    } else if (newSource == "Spotify") {
        m_currentProvider = m_spotifyClient.get();
    } else if (newSource == "SoundCloud") {
        m_currentProvider = m_soundCloudClient.get();
    } else if (newSource == "Yandex") {
        m_currentProvider = m_yandexClient.get();
    } else if (newSource == "YouTube") {
        m_currentProvider = m_youtubeClient.get();
    } else if (newSource == "Offline" || newSource == "All" || newSource.rfind("Custom:", 0) == 0) {
        m_currentProvider = nullptr;
    }

    emit SourceChanged(newSource);

    if (newSource == "VK") {
        StartVkService();
    } else if (newSource == "Spotify") {
        StartSpotifyService();
    } else if (newSource == "SoundCloud") {
        StartSoundCloudService();
    } else if (newSource == "Yandex") {
        StartYandexService();
    } else if (newSource == "YouTube") {
        StartYouTubeService();
    } else if (newSource == "Offline") {
        emit AuthUiStateChanged(false);
        emit ProviderReady(false);
    } else if (newSource == "All" || newSource.rfind("Custom:", 0) == 0) {
        emit AuthUiStateChanged(false);
        emit ProviderReady(true);
    }
}

IAudioProvider* SourceRouter::GetProvider(const std::string& sourceName) const {
    if (sourceName == "VK" || sourceName == "vk") return m_vkClient.get();
    if (sourceName == "Spotify" || sourceName == "spotify") return m_spotifyClient.get();
    if (sourceName == "SoundCloud" || sourceName == "soundcloud" || sourceName == "sc") return m_soundCloudClient.get();
    if (sourceName == "Yandex" || sourceName == "yandex") return m_yandexClient.get();
    if (sourceName == "YouTube" || sourceName == "youtube" || sourceName == "yt") return m_youtubeClient.get();
    return nullptr;
}

void SourceRouter::Logout(const std::string& service) {
    if (m_authEngine) {
        m_authEngine->deleteLater();
        m_authEngine = nullptr;
        emit AuthUiStateChanged(false);
    }

    auto processLogout = [this](const QString& svcName, IAudioProvider* client) {
        m_authManager->ClearSavedToken(svcName);
        WebViewCookieReader::ClearServiceCache(svcName.toStdString());
        if (client) client->SetAccessToken("");
    };

    std::string lowerSvc = service;
    for (char& c : lowerSvc) c = std::tolower(c);

    if (lowerSvc == "vk" || lowerSvc == "all") processLogout("VK", m_vkClient.get());
    if (lowerSvc == "spotify" || lowerSvc == "all") processLogout("Spotify", m_spotifyClient.get());
    if (lowerSvc == "sc" || lowerSvc == "soundcloud" || lowerSvc == "all") processLogout("SoundCloud", m_soundCloudClient.get());
    if (lowerSvc == "yandex" || lowerSvc == "all") processLogout("Yandex", m_yandexClient.get());
    if (lowerSvc == "youtube" || lowerSvc == "yt" || lowerSvc == "all") processLogout("YouTube", m_youtubeClient.get());
}

void SourceRouter::CheckSourceAuthorized(const std::string& source, std::function<void(bool isAuth)> callback) const {
    if (source == "VK") {
        m_authManager->GetSavedToken("VK", [callback](const std::string& token) {
            callback(!token.empty());
        });
    } else if (source == "Yandex") {
        m_authManager->GetSavedToken("Yandex", [callback](const std::string& token) {
            callback(!token.empty());
        });
    } else if (source == "SoundCloud") {
        m_authManager->GetSavedToken("SoundCloud", [callback](const std::string& token) {
            callback(!token.empty());
        });
    } else if (source == "Spotify") {
        QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
        if (!spDc.isEmpty()) {
            callback(true);
            return;
        }
        m_authManager->GetSavedToken("Spotify", [callback](const std::string& token) {
            callback(!token.empty());
        });
    } else if (source == "YouTube") {
        QString envCookie = m_envVars.value("YOUTUBE_COOKIE", "");
        if (!envCookie.isEmpty()) {
            callback(true);
            return;
        }
        m_authManager->GetSavedToken("YouTube", [callback](const std::string& savedToken) {
            std::string token = savedToken;
            if (token.find("LOGIN_INFO=") == std::string::npos) {
                std::string fullCookies = WebViewCookieReader::GetFullYouTubeCookies();
                if (fullCookies.find("LOGIN_INFO=") != std::string::npos) {
                    token = fullCookies;
                }
            }
            bool hasValidCookies = (token.find("LOGIN_INFO=") != std::string::npos);
            callback(hasValidCookies);
        });
    } else {
        callback(false);
    }
}

void SourceRouter::FindNextAuthorizedSource(const std::string& excludedSource, std::function<void(const std::string& nextSource)> callback) const {
    if (excludedSource == "all" || excludedSource == "ALL") {
        if (callback) callback("Offline");
        return;
    }

    const std::vector<std::string> allSources = {"VK", "Yandex", "Spotify", "SoundCloud", "YouTube"};
    auto candidates = std::make_shared<std::vector<std::string>>();
    candidates->reserve(allSources.size());
    for (const auto& s : allSources) {
        if (QString::compare(QString::fromStdString(s), QString::fromStdString(excludedSource), Qt::CaseInsensitive) != 0) {
            candidates->push_back(s);
        }
    }

    CheckNextCandidate(candidates, 0, std::move(callback));
}

void SourceRouter::CheckNextCandidate(const std::shared_ptr<const std::vector<std::string>>& candidates,
                                      size_t index,
                                      std::function<void(const std::string& nextSource)> callback) const {
    if (!candidates || index >= candidates->size()) {
        if (callback) callback("Offline");
        return;
    }

    QPointer<const SourceRouter> safeThis(this);
    const std::string cand = (*candidates)[index];

    CheckSourceAuthorized(cand, [safeThis, candidates, index, cand, callback = std::move(callback)](bool isAuth) mutable {
        if (!safeThis) return;
        if (isAuth) {
            if (callback) callback(cand);
        } else {
            safeThis->CheckNextCandidate(candidates, index + 1, std::move(callback));
        }
    });
}