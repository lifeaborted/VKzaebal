#include "SourceRouter.h"
#include "core/auth/oauth/OAuthManager.h"
#include "core/auth/oauth/WebViewCookieReader.h"
#include "core/api/vk/VkClient.h"
#include "core/api/spotify/SpotifyClient.h"
#include "core/api/soundcloud/SoundCloudClient.h"
#include "core/api/yandex/YandexClient.h"
#include "core/api/youtube/YouTubeClient.h"
#include "utils/logger/Logger.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>
#include <QUrl>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <algorithm>

namespace {
constexpr const char* kVkAuthUrl = "https://oauth.vk.com/authorize?client_id=6287487&display=page&redirect_uri=https://oauth.vk.com/blank.html&scope=audio,offline&response_type=token&v=5.131";
}

SourceRouter::SourceRouter(const QMap<QString, QString>& envVars,
                           QNetworkAccessManager* networkManager,
                           QObject* parent)
    : QObject(parent), m_networkManager(networkManager), m_envVars(envVars) {
    m_authManager = std::make_unique<OAuthManager>(this, m_networkManager);
    m_authManager->ClearSavedCookies("VK");

    // Прием универсального токена из WebView
    connect(m_authManager.get(), &OAuthManager::TokenReceived, this, [this](const std::string& token) {
        if (m_currentAuthService == "VK") {
            OnVkTokenReceived(token);
        } else if (m_currentAuthService == "Spotify") {
            OnSpotifyTokenReceived(token);
        } else if (m_currentAuthService == "SoundCloud") {
            if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }

            m_authManager->SaveToken(token, "SoundCloud");
            emit AuthUiStateChanged(false);
            EmitStatus("[УСПЕХ] Авторизация SoundCloud пройдена! Токен перехвачен.");

            auto* sc = GetSoundCloudClient();
            if (sc) {
                sc->SetAccessToken(token);
                sc->InitializeWithToken();
            }
            emit ProviderReady(true);
        } else if (m_currentAuthService == "Yandex") {
            if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
            m_authManager->SaveToken(token, "Yandex");
            m_authManager->ClearSavedUserId("Yandex");
            emit AuthUiStateChanged(false);
            EmitStatus("[УСПЕХ] Авторизация Yandex пройдена!");

            auto* ya = GetYandexClient();
            if (ya) {
                ya->SetAccessToken(token);
                ya->SetUserId("");
                ya->FetchAllUserAudio(0, 200);
            }
            emit ProviderReady(true);
        }
    });

    // Перехват успеха авторизации YouTube
    connect(m_authManager.get(), &OAuthManager::YtAuthSucceeded, this, [this](const std::string& cookies) {
        if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }

        emit AuthUiStateChanged(false);
        EmitStatus("[УСПЕХ] Авторизация YouTube Music пройдена! Синхронизация избранных треков...");

        auto* yt = GetYouTubeClient();
        if (yt) {
            yt->SetAccessToken(cookies);
            yt->FetchAllUserAudio(0, 100);
        }
        emit ProviderReady(true);
    });

    connect(m_authManager.get(), &OAuthManager::AuthCodeReceived, this, [this](const std::string& code) {
        static std::string lastCode = "";
        if (code == lastCode) return;
        lastCode = code;

        if (m_currentAuthService == "Spotify") {
            if (m_authEngine) {
                m_authEngine->deleteLater();
                m_authEngine = nullptr;
                emit AuthUiStateChanged(false);
                EmitStatus("[Инфо] Код перехвачен. Закрываем окно авторизации...");
            }

            Logger::Log(LogLevel::INFO, "SourceRouter: Exchanging Spotify code for token...");
            auto* sp = GetSpotifyClient();
            if (sp) {
                sp->ExchangeCodeForToken(code);
            }
        }
    });
}

SourceRouter::~SourceRouter() {
    if (m_silentAuthTimer) {
        m_silentAuthTimer->stop();
        m_silentAuthTimer = nullptr;
    }
    if (m_authEngine) {
        m_authEngine->deleteLater();
    }
}

void SourceRouter::EmitStatus(const std::string& msg) {
    Logger::Log(LogLevel::INFO, "SourceRouter: " + msg);
    emit StatusMessageRequested(msg);
}

IAudioProvider* SourceRouter::GetOrCreateProvider(const std::string& sourceName) {
    std::string key = sourceName;
    if (key == "vk" || key == "VK") key = "VK";
    else if (key == "spotify" || key == "Spotify") key = "Spotify";
    else if (key == "soundcloud" || key == "sc" || key == "SoundCloud") key = "SoundCloud";
    else if (key == "yandex" || key == "Yandex") key = "Yandex";
    else if (key == "youtube" || key == "yt" || key == "YouTube") key = "YouTube";
    else return nullptr;

    auto it = m_providers.find(key);
    if (it != m_providers.end()) {
        return it->second.get();
    }

    std::unique_ptr<IAudioProvider> provider;
    if (key == "VK") {
        auto vk = std::make_unique<VkClient>(this, m_networkManager);
        connect(vk.get(), &VkClient::TokenExpired, this, &SourceRouter::OnVkTokenExpired);
        connect(vk.get(), &VkClient::UserBlocked, this, [this](const std::string& service, const std::string& reason) {
            Logger::Log(LogLevel::ERROR, "SourceRouter: User blocked on " + service + ": " + reason);
            EmitStatus("[ВНИМАНИЕ] Аккаунт " + service + " временно заморожен! Открываем окно для разморозки...");
            emit AuthUiStateChanged(true);
            StartAuthFlow(QString::fromStdString(service), kVkAuthUrl, /*forceVisible=*/true);
        });
        provider = std::move(vk);
    } else if (key == "Spotify") {
        auto sp = std::make_unique<SpotifyClient>(this, m_networkManager);
        connect(sp.get(), &SpotifyClient::TokenReceived, this, &SourceRouter::OnSpotifyTokenReceived);
        connect(sp.get(), &SpotifyClient::AuthError, this, &SourceRouter::OnSpotifyAuthError);
        provider = std::move(sp);
    } else if (key == "SoundCloud") {
        provider = std::make_unique<SoundCloudClient>(this, m_networkManager);
    } else if (key == "Yandex") {
        auto ya = std::make_unique<YandexClient>(this, m_networkManager);
        connect(ya.get(), &YandexClient::UserIdFetched, this, [this](const std::string& uid) {
            m_authManager->SaveUserId(uid, "Yandex");
        });
        provider = std::move(ya);
    } else if (key == "YouTube") {
        auto yt = std::make_unique<YouTubeClient>(this, m_networkManager);
        connect(yt.get(), &YouTubeClient::TokenExpired, this, [this]() {
            Logger::Log(LogLevel::WARNING, "SourceRouter: YouTube session expired or BotGuard rejected.");
            EmitStatus("[ВНИМАНИЕ] Сессия YouTube Music устарела или отклонена.");
            m_authManager->ClearSavedToken("YouTube");
            auto* ytc = GetYouTubeClient();
            if (ytc) ytc->SetAccessToken("");
            StartAuthFlow("YouTube", "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F");
        });
        provider = std::move(yt);
    }

    IAudioProvider* ptr = provider.get();
    if (ptr) {
        connect(ptr, &IAudioProvider::AudioFetched, this, [this, ptr](const std::vector<Track>& t) {
            if (m_currentProvider == ptr) emit AudioFetched(t);
        });
        connect(ptr, &IAudioProvider::FinishedFetching, this, [this, ptr]() {
            if (m_currentProvider == ptr) emit FinishedFetching();
        });
    }
    m_providers[key] = std::move(provider);
    return ptr;
}

VkClient* SourceRouter::GetVkClient() const {
    return static_cast<VkClient*>(const_cast<SourceRouter*>(this)->GetOrCreateProvider("VK"));
}

SpotifyClient* SourceRouter::GetSpotifyClient() const {
    return static_cast<SpotifyClient*>(const_cast<SourceRouter*>(this)->GetOrCreateProvider("Spotify"));
}

SoundCloudClient* SourceRouter::GetSoundCloudClient() const {
    return static_cast<SoundCloudClient*>(const_cast<SourceRouter*>(this)->GetOrCreateProvider("SoundCloud"));
}

YandexClient* SourceRouter::GetYandexClient() const {
    return static_cast<YandexClient*>(const_cast<SourceRouter*>(this)->GetOrCreateProvider("Yandex"));
}

YouTubeClient* SourceRouter::GetYouTubeClient() const {
    return static_cast<YouTubeClient*>(const_cast<SourceRouter*>(this)->GetOrCreateProvider("YouTube"));
}

void SourceRouter::StartAuthFlow(const QString& service, const QString& authUrl, bool forceVisible) {
    if (m_isAuthFlowActive && m_authEngine) {
        Logger::Log(LogLevel::WARNING, "SourceRouter: Auth flow already active for " + m_currentAuthService.toStdString());
        return;
    }
    m_isAuthFlowActive = true;
    m_currentAuthService = service;
    Logger::Log(LogLevel::INFO, "SourceRouter: Starting auth flow via QML for " + service.toStdString() +
                                (forceVisible ? " (visible)..." : " (silent)..."));

    if (forceVisible) {
        emit AuthUiStateChanged(true);
        EmitStatus("=== Авторизация " + service.toStdString() + " === Откроется окно браузера.");
    } else {
        EmitStatus("[" + service.toStdString() + "] Фоновое обновление сессии...");
    }

    if (m_silentAuthTimer) {
        m_silentAuthTimer->stop();
        m_silentAuthTimer->deleteLater();
        m_silentAuthTimer = nullptr;
    }

    if (!forceVisible) {
        m_silentAuthTimer = new QTimer(this);
        m_silentAuthTimer->setSingleShot(true);
        connect(m_silentAuthTimer, &QTimer::timeout, this, [this, service, authUrl]() {
            Logger::Log(LogLevel::WARNING, "SourceRouter: Silent auth timeout for " + service.toStdString() + ". Falling back to visible auth.");
            if (m_authEngine) {
                m_authEngine->deleteLater();
                m_authEngine = nullptr;
            }
            m_isAuthFlowActive = false;
            EmitStatus("[" + service.toStdString() + "] Фоновое обновление не удалось (требуется вход). Открываем окно...");
            StartAuthFlow(service, authUrl, /*forceVisible=*/true);
        });
        m_silentAuthTimer->start(7000);
    }

    if (m_authEngine) {
        m_authEngine->deleteLater();
        m_authEngine = nullptr;
    }

    m_authEngine = new QQmlApplicationEngine();
    m_authEngine->rootContext()->setContextProperty("cppAuthManager", m_authManager.get());
    m_authEngine->rootContext()->setContextProperty("cppAuthUrl", authUrl);
    m_authEngine->rootContext()->setContextProperty("cppForceVisible", forceVisible);
    m_authEngine->load(QUrl(QStringLiteral("qrc:/core/auth/auth.qml")));

    if (m_authEngine->rootObjects().isEmpty()) {
        Logger::Log(LogLevel::ERROR, "SourceRouter: Failed to load auth.qml!");
        if (m_silentAuthTimer) {
            m_silentAuthTimer->stop();
            m_silentAuthTimer->deleteLater();
            m_silentAuthTimer = nullptr;
        }
        m_authEngine->deleteLater();
        m_authEngine = nullptr;
        m_isAuthFlowActive = false;
        emit AuthUiStateChanged(false);
    } else {
        QWindow* rootWindow = qobject_cast<QWindow*>(m_authEngine->rootObjects().first());
        if (rootWindow) {
            connect(rootWindow, &QWindow::visibleChanged, this, [this, forceVisible](bool visible) {
                if (!visible && forceVisible && m_authEngine) {
                    if (m_silentAuthTimer) {
                        m_silentAuthTimer->stop();
                        m_silentAuthTimer->deleteLater();
                        m_silentAuthTimer = nullptr;
                    }
                    m_authEngine->deleteLater();
                    m_authEngine = nullptr;
                    m_isAuthFlowActive = false;
                    emit AuthUiStateChanged(false);
                    EmitStatus("[Инфо] Окно авторизации закрыто.");
                }
            });
        }
    }
}

void SourceRouter::OnVkTokenReceived(const std::string& token) {
    if (m_silentAuthTimer) {
        m_silentAuthTimer->stop();
        m_silentAuthTimer->deleteLater();
        m_silentAuthTimer = nullptr;
    }
    m_isAuthFlowActive = false;
    if (m_authEngine) { m_authEngine->deleteLater(); m_authEngine = nullptr; }
    m_authManager->SaveToken(token, "VK");

    emit AuthUiStateChanged(false);
    EmitStatus("[УСПЕХ] Авторизация VK успешно завершена!");

    auto* vk = GetVkClient();
    if (vk) {
        vk->SetAccessToken(token);
        vk->FetchAllUserAudio(0, 200);
    }
    emit ProviderReady(true);
}

void SourceRouter::OnSpotifyTokenReceived(const std::string& token) {
    m_isAuthFlowActive = false;
    m_authManager->SaveToken(token, "Spotify");
    emit AuthUiStateChanged(false);
    EmitStatus("[УСПЕХ] Авторизация Spotify пройдена!");

    auto* sp = GetSpotifyClient();
    if (sp) {
        sp->SetAccessToken(token);
        sp->FetchAllUserAudio(0, 50);
    }
    emit ProviderReady(true);
}

void SourceRouter::OnSpotifyAuthError(const std::string& err) {
    m_isAuthFlowActive = false;
    EmitStatus("[ОШИБКА] Не удалось получить токен Spotify: " + err);
    emit AuthUiStateChanged(false);
}

void SourceRouter::OnVkTokenExpired() {
    if (m_isAuthFlowActive) {
        Logger::Log(LogLevel::INFO, "SourceRouter: Auth already in progress, skipping duplicate OnVkTokenExpired.");
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAuthAttemptMs < 15000) {
        Logger::Log(LogLevel::WARNING, "SourceRouter: Rate limiting VK auth flow (cooldown 15s).");
        return;
    }
    m_lastAuthAttemptMs = now;

    Logger::Log(LogLevel::WARNING, "SourceRouter: VK token expired. Attempting silent renewal in background...");
    auto* vk = GetVkClient();
    if (vk) vk->SetAccessToken("");
    m_authManager->ClearSavedToken("VK");
    StartAuthFlow("VK", kVkAuthUrl, /*forceVisible=*/false);
}

void SourceRouter::StartVkService() {
    QPointer<SourceRouter> safeThis(this);
    m_authManager->GetSavedToken("VK", [safeThis](const std::string& savedToken) {
        if (!safeThis) return;
        if (savedToken.empty()) {
            safeThis->EmitStatus("[VK] Токен не найден. Открываем окно авторизации...");
            safeThis->StartAuthFlow("VK", kVkAuthUrl, /*forceVisible=*/true);
        } else {
            safeThis->EmitStatus("[VK] Проверка сохраненного токена...");
            auto* vk = safeThis->GetVkClient();
            if (vk) {
                vk->SetAccessToken(savedToken);
                vk->ValidateToken([safeThis, vk, savedToken](bool isValid) {
                    if (!safeThis) return;
                    if (isValid) {
                        Logger::Log(LogLevel::INFO, "SourceRouter: Saved VK token is valid.");
                        emit safeThis->AuthUiStateChanged(false);
                        emit safeThis->ProviderReady(true);
                        vk->FetchAllUserAudio(0, 200);
                    } else {
                        safeThis->EmitStatus("[VK] Сохраненный токен устарел. Обновление сессии в фоновом режиме...");
                        if (vk) vk->SetAccessToken("");
                        safeThis->m_authManager->ClearSavedToken("VK");
                        safeThis->StartAuthFlow("VK", kVkAuthUrl, /*forceVisible=*/false);
                    }
                });
            }
        }
    });
}

void SourceRouter::StartSoundCloudService() {
    m_authManager->GetSavedToken("SoundCloud", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            EmitStatus("[SoundCloud] Токен не найден. Открываем окно авторизации...");
            StartAuthFlow("SoundCloud", "https://soundcloud.com/signin");
        } else {
            EmitStatus("[SoundCloud] Инициализация по сохраненному токену...");
            auto* sc = GetSoundCloudClient();
            if (sc) {
                sc->SetAccessToken(savedToken);
                sc->InitializeWithToken();
            }
            emit ProviderReady(true);
        }
    });
}

void SourceRouter::StartSpotifyService() {
    QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
    QString clientId = m_envVars.value("SPOTIFY_CLIENT_ID", "");

    m_authManager->GetSavedToken("Spotify", [this, spDc, clientId](const std::string& savedToken) {
        auto* sp = GetSpotifyClient();
        if (!sp) return;

        if (!spDc.isEmpty()) {
            if (savedToken.empty()) {
                EmitStatus("[Spotify] Получение Web Access Token через sp_dc...");
                sp->AuthWithSpDc(spDc);
            } else {
                sp->SetAccessToken(savedToken);
                EmitStatus("[Spotify] Проверка сохраненного токена (sp_dc)...");

                sp->ValidateToken([this, spDc, sp](bool isValid) {
                    if (isValid) {
                        emit AuthUiStateChanged(false);
                        EmitStatus("[УСПЕХ] Синхронизация треков Spotify...");
                        emit ProviderReady(true);
                        sp->FetchAllUserAudio(0, 50);
                    } else {
                        EmitStatus("[ВНИМАНИЕ] Токен Spotify устарел. Тихое обновление...");
                        m_authManager->ClearSavedToken("Spotify");
                        sp->SetAccessToken("");
                        sp->AuthWithSpDc(spDc);
                    }
                });
            }
        } else if (!clientId.isEmpty()) {
            if (savedToken.empty()) {
                std::string authUrl = sp->StartAuthPkce(clientId);
                StartAuthFlow("Spotify", QString::fromStdString(authUrl));
            } else {
                sp->SetAccessToken(savedToken);
                EmitStatus("[Spotify] Проверка сохраненного токена (PKCE)...");

                sp->ValidateToken([this, clientId, sp](bool isValid) {
                    if (isValid) {
                        emit AuthUiStateChanged(false);
                        EmitStatus("[УСПЕХ] Синхронизация треков Spotify...");
                        emit ProviderReady(true);
                        sp->FetchAllUserAudio(0, 50);
                    } else {
                        EmitStatus("[ВНИМАНИЕ] Токен Spotify устарел. Открытие окна авторизации...");
                        m_authManager->ClearSavedToken("Spotify");
                        sp->SetAccessToken("");
                        std::string authUrl = sp->StartAuthPkce(clientId);
                        StartAuthFlow("Spotify", QString::fromStdString(authUrl));
                    }
                });
            }
        } else {
            emit AuthUiStateChanged(false);
            EmitStatus("[ОШИБКА] В .env не задан ни SPOTIFY_SP_DC, ни SPOTIFY_CLIENT_ID!");
        }
    });
}

void SourceRouter::StartYandexService() {
    m_authManager->GetSavedToken("Yandex", [this](const std::string& savedToken) {
        if (savedToken.empty()) {
            EmitStatus("[Yandex] Токен не найден. Открываем окно авторизации...");
            StartAuthFlow("Yandex", "https://oauth.yandex.ru/authorize?response_type=token&client_id=23cabbbdc6cd418abb4b39c32c41195d");
        } else {
            EmitStatus("[Yandex] Инициализация по сохраненному токену...");
            auto* ya = GetYandexClient();
            if (ya) {
                ya->SetAccessToken(savedToken);
                QPointer<SourceRouter> safeThis(this);
                m_authManager->GetSavedUserId("Yandex", [safeThis, ya](const std::string& savedUid) {
                    if (safeThis && ya && !savedUid.empty()) {
                        ya->SetUserId(savedUid);
                        Logger::Log(LogLevel::INFO, "SourceRouter: Loaded cached Yandex UID: " + savedUid);
                    }
                    if (ya) ya->FetchAllUserAudio(0, 200);
                    if (safeThis) emit safeThis->ProviderReady(true);
                });
            }
        }
    });
}

void SourceRouter::StartYouTubeService() {
    QString envCookie = m_envVars.value("YOUTUBE_COOKIE", "");

    m_authManager->GetSavedToken("YouTube", [this, envCookie](const std::string& savedToken) {
        std::string effectiveToken = !envCookie.isEmpty() ? envCookie.toStdString() : savedToken;

        if (effectiveToken.empty()) {
            std::string fullCookies = WebViewCookieReader::GetFullYouTubeCookies();
            if (!fullCookies.empty() &&
                (fullCookies.find("SAPISID=") != std::string::npos || fullCookies.find("__Secure-1PAPISID=") != std::string::npos)) {
                Logger::Log(LogLevel::INFO, "SourceRouter: Retrieved active YouTube session from WebView2 storage.");
                effectiveToken = fullCookies;
                m_authManager->SaveToken(effectiveToken, "YouTube");
            }
        }

        bool hasValidCookies = !effectiveToken.empty() &&
                               (effectiveToken.find("SAPISID=") != std::string::npos ||
                                effectiveToken.find("__Secure-1PAPISID=") != std::string::npos ||
                                effectiveToken.find("__Secure-3PAPISID=") != std::string::npos);

        auto* yt = GetYouTubeClient();

        if (!hasValidCookies) {
            if (!effectiveToken.empty()) {
                m_authManager->ClearSavedToken("YouTube");
            }
            EmitStatus("[YouTube] Требуется авторизация для загрузки медиатеки...");
            StartAuthFlow("YouTube", "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F", /*forceVisible=*/true);
        } else {
            EmitStatus("[YouTube] Сессия найдена. Загрузка избранных треков...");
            if (yt) {
                yt->SetAccessToken(effectiveToken);
                yt->FetchAllUserAudio(0, 100);
            }
            emit AuthUiStateChanged(false);
            emit ProviderReady(true);
        }
    });
}

void SourceRouter::SwitchSource(const std::string& newSource) {
    Logger::Log(LogLevel::INFO, "SourceRouter: Switching audio source to " + newSource);

    if (newSource == "Offline" || newSource == "All" || newSource.rfind("Custom:", 0) == 0) {
        m_currentProvider = nullptr;
    } else {
        m_currentProvider = GetOrCreateProvider(newSource);
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
        EnsureAllProvidersInitialized();
        emit AuthUiStateChanged(false);
        emit ProviderReady(true);
    }
}

IAudioProvider* SourceRouter::GetProvider(const std::string& sourceName) const {
    if (sourceName == "VK" || sourceName == "vk") {
        auto* vk = GetVkClient();
        if (vk && vk->GetAccessToken().empty()) {
            const_cast<SourceRouter*>(this)->PreinitializeVkClient();
        }
        return vk;
    }
    if (sourceName == "Spotify" || sourceName == "spotify") {
        auto* sp = GetSpotifyClient();
        if (sp && sp->GetAccessToken().empty()) {
            const_cast<SourceRouter*>(this)->PreinitializeSpotifyClient();
        }
        return sp;
    }
    if (sourceName == "SoundCloud" || sourceName == "soundcloud" || sourceName == "sc") {
        auto* sc = GetSoundCloudClient();
        if (sc && sc->GetAccessToken().empty()) {
            const_cast<SourceRouter*>(this)->PreinitializeSoundCloudClient();
        }
        return sc;
    }
    if (sourceName == "Yandex" || sourceName == "yandex") {
        auto* ya = GetYandexClient();
        if (ya && ya->GetAccessToken().empty()) {
            const_cast<SourceRouter*>(this)->PreinitializeYandexClient();
        }
        return ya;
    }
    if (sourceName == "YouTube" || sourceName == "youtube" || sourceName == "yt") {
        auto* yt = GetYouTubeClient();
        if (yt && yt->GetAccessToken().empty()) {
            const_cast<SourceRouter*>(this)->PreinitializeYouTubeClient();
        }
        return yt;
    }
    return nullptr;
}

void SourceRouter::Logout(const std::string& service) {
    if (m_silentAuthTimer) {
        m_silentAuthTimer->stop();
        m_silentAuthTimer->deleteLater();
        m_silentAuthTimer = nullptr;
    }
    if (m_authEngine) {
        m_authEngine->deleteLater();
        m_authEngine = nullptr;
        emit AuthUiStateChanged(false);
    }

    auto processLogout = [this](const QString& svcName, IAudioProvider* client) {
        m_authManager->ClearSavedToken(svcName);
        m_authManager->ClearSavedCookies(svcName);
        m_authManager->ClearSavedUserId(svcName);
        WebViewCookieReader::ClearServiceCache(svcName.toStdString());
        if (client) client->SetAccessToken("");
        if (svcName == "Yandex") {
            auto* ya = GetYandexClient();
            if (ya) ya->SetUserId("");
        }
    };

    std::string lowerSvc = service;
    for (char& c : lowerSvc) c = std::tolower(c);

    auto getExistingProvider = [this](const std::string& name) -> IAudioProvider* {
        auto it = m_providers.find(name);
        return it != m_providers.end() ? it->second.get() : nullptr;
    };

    if (lowerSvc == "vk" || lowerSvc == "all") processLogout("VK", getExistingProvider("VK"));
    if (lowerSvc == "spotify" || lowerSvc == "all") processLogout("Spotify", getExistingProvider("Spotify"));
    if (lowerSvc == "sc" || lowerSvc == "soundcloud" || lowerSvc == "all") processLogout("SoundCloud", getExistingProvider("SoundCloud"));
    if (lowerSvc == "yandex" || lowerSvc == "all") processLogout("Yandex", getExistingProvider("Yandex"));
    if (lowerSvc == "youtube" || lowerSvc == "yt" || lowerSvc == "all") processLogout("YouTube", getExistingProvider("YouTube"));
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
            if (token.empty()) {
                std::string fullCookies = WebViewCookieReader::GetFullYouTubeCookies();
                if (!fullCookies.empty() &&
                    (fullCookies.find("SAPISID=") != std::string::npos || fullCookies.find("__Secure-1PAPISID=") != std::string::npos)) {
                    token = fullCookies;
                }
            }
            bool hasValidCookies = (!token.empty() &&
                                   (token.find("SAPISID=") != std::string::npos ||
                                    token.find("__Secure-1PAPISID=") != std::string::npos ||
                                    token.find("__Secure-3PAPISID=") != std::string::npos));
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

void SourceRouter::PreinitializeVkClient() {
    auto* vk = GetVkClient();
    if (!vk || !vk->GetAccessToken().empty()) return;

    QPointer<SourceRouter> safeThis(this);
    m_authManager->GetSavedToken("VK", [safeThis](const std::string& savedToken) {
        if (!safeThis || savedToken.empty()) return;

        auto* vkClient = safeThis->GetVkClient();
        if (vkClient && vkClient->GetAccessToken().empty()) {
            vkClient->SetAccessToken(savedToken);
            Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized VK client with saved token.");
        }
    });
}

void SourceRouter::PreinitializeYandexClient() {
    auto* ya = GetYandexClient();
    if (ya && ya->GetAccessToken().empty()) {
        QPointer<SourceRouter> safeThis(this);
        m_authManager->GetSavedToken("Yandex", [safeThis](const std::string& token) {
            if (!safeThis || token.empty()) return;
            auto* yaClient = safeThis->GetYandexClient();
            if (yaClient && yaClient->GetAccessToken().empty()) {
                yaClient->SetAccessToken(token);
                safeThis->m_authManager->GetSavedUserId("Yandex", [safeThis, yaClient](const std::string& uid) {
                    if (safeThis && yaClient && !uid.empty()) {
                        yaClient->SetUserId(uid);
                        Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized Yandex client with cached UID: " + uid);
                    }
                });
                Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized Yandex client with saved token.");
            }
        });
    }
}

void SourceRouter::PreinitializeSoundCloudClient() {
    auto* sc = GetSoundCloudClient();
    if (sc && sc->GetAccessToken().empty()) {
        QPointer<SourceRouter> safeThis(this);
        m_authManager->GetSavedToken("SoundCloud", [safeThis](const std::string& token) {
            if (!safeThis || token.empty()) return;
            auto* scClient = safeThis->GetSoundCloudClient();
            if (scClient && scClient->GetAccessToken().empty()) {
                scClient->SetAccessToken(token);
                scClient->InitializeWithToken();
                Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized SoundCloud client with saved token.");
            }
        });
    }
}

void SourceRouter::PreinitializeSpotifyClient() {
    auto* sp = GetSpotifyClient();
    if (sp && sp->GetAccessToken().empty()) {
        QString spDc = m_envVars.value("SPOTIFY_SP_DC", "");
        if (!spDc.isEmpty()) {
            sp->AuthWithSpDc(spDc);
        } else {
            QPointer<SourceRouter> safeThis(this);
            m_authManager->GetSavedToken("Spotify", [safeThis](const std::string& token) {
                if (!safeThis || token.empty()) return;
                auto* spClient = safeThis->GetSpotifyClient();
                if (spClient && spClient->GetAccessToken().empty()) {
                    spClient->SetAccessToken(token);
                    Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized Spotify client with saved token.");
                }
            });
        }
    }
}

void SourceRouter::PreinitializeYouTubeClient() {
    auto* yt = GetYouTubeClient();
    if (yt && yt->GetAccessToken().empty()) {
        QString envCookie = m_envVars.value("YOUTUBE_COOKIE", "");
        QPointer<SourceRouter> safeThis(this);
        m_authManager->GetSavedToken("YouTube", [safeThis, envCookie](const std::string& savedToken) {
            if (!safeThis) return;
            auto* ytClient = safeThis->GetYouTubeClient();
            if (!ytClient) return;

            std::string token = !envCookie.isEmpty() ? envCookie.toStdString() : savedToken;
            if (token.empty()) {
                std::string fullCookies = WebViewCookieReader::GetFullYouTubeCookies();
                if (!fullCookies.empty() &&
                    (fullCookies.find("SAPISID=") != std::string::npos || fullCookies.find("__Secure-1PAPISID=") != std::string::npos)) {
                    token = fullCookies;
                    safeThis->m_authManager->SaveToken(token, "YouTube");
                }
            }
            if (!token.empty() && ytClient->GetAccessToken().empty()) {
                ytClient->SetAccessToken(token);
                Logger::Log(LogLevel::INFO, "SourceRouter: Pre-initialized YouTube client with saved cookies.");
            }
        });
    }
}

void SourceRouter::EnsureAllProvidersInitialized() {
    PreinitializeVkClient();
    PreinitializeYandexClient();
    PreinitializeSoundCloudClient();
    PreinitializeSpotifyClient();
    PreinitializeYouTubeClient();
}