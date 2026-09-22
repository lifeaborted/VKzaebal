import QtQuick
import QtQuick.Window
import QtWebView
import QtQml

Window {
    id: authWindow
    width: 900
    height: 700
    color: "#181818"
    visible: true
    title: "Авторизация"

    property bool vkSilentTokenRedirected: false

    WebView {
        id: webView
        anchors.fill: parent
        url: cppAuthUrl

        onLoadingChanged: function(loadRequest) {
            console.warn("[QML] Loading status=" + loadRequest.status + " error=" + loadRequest.errorString + " url=" + loadRequest.url)
        }

        onUrlChanged: {
            var currentStr = url.toString()
            console.warn("[QML] Текущий URL: " + currentStr)

            // Автоматический переход после авторизации через VK ID (по QR-коду):
            // Когда VK ID возвращает silent_token или payload без access_token, сессия пользователя уже
            // успешно установлена в браузере. Немедленно перенаправляем на OAuth приложения!
            if (!authWindow.vkSilentTokenRedirected &&
                (currentStr.indexOf("silent_token") !== -1 ||
                 currentStr.indexOf("auth_redirect") !== -1 ||
                 (currentStr.indexOf("blank.html#payload") !== -1 && currentStr.indexOf("access_token=") === -1))) {
                authWindow.vkSilentTokenRedirected = true
                console.warn("[QML] VK ID: обнаружен silent_token / payload. Немедленно перенаправляем на OAuth...")
                webView.url = cppAuthUrl
                return
            }

            cppAuthManager.onUrlIntercepted(currentStr)
        }
    }

    Timer {
        id: universalSniper
        interval: 300
        running: true
        repeat: true
        onTriggered: {
            // --- SOUNDCLOUD ---
            if (cppAuthUrl.indexOf("soundcloud.com") !== -1) {
                var scCode = `
                    (function() {
                        var token = window.localStorage.getItem('oauth_token');
                        if (token) return token;
                        var cookies = document.cookie.split(';');
                        for (var i = 0; i < cookies.length; i++) {
                            var c = cookies[i].trim();
                            if (c.indexOf('oauth_token=') === 0) {
                                return c.substring('oauth_token='.length, c.length);
                            }
                        }
                        return "";
                    })();
                `;
                webView.runJavaScript(scCode, function(result) {
                    if (result && result !== "null" && result !== "") {
                        console.log("[QML] SoundCloud Token intercepted.");
                        cppAuthManager.onScTokenIntercepted(result);
                        universalSniper.running = false;
                    }
                });
            }
            // --- ВКОНТАКТЕ ---
            else if (cppAuthUrl.indexOf("oauth.vk.com") !== -1 || cppAuthUrl.indexOf("oauth.vk.ru") !== -1 || cppAuthUrl.indexOf("id.vk.com") !== -1) {
                var vkCode = `
                    (function() {
                        var h = window.location.href;
                        if (h.indexOf('silent_token') !== -1 || h.indexOf('auth_redirect') !== -1 || (h.indexOf('blank.html#payload') !== -1 && h.indexOf('access_token=') === -1)) {
                            return "needs_redirect";
                        }
                        return "waiting";
                    })();
                `;
                webView.runJavaScript(vkCode, function(result) {
                    if (result === "needs_redirect" && !authWindow.vkSilentTokenRedirected) {
                        authWindow.vkSilentTokenRedirected = true;
                        console.warn("[QML] VK ID sniper detected silent_token, redirecting to cppAuthUrl...");
                        webView.url = cppAuthUrl;
                    }
                });
            }
            // --- YOUTUBE MUSIC ---
            else if (cppAuthUrl.indexOf("youtube.com") !== -1 || cppAuthUrl.indexOf("google.com") !== -1) {
                var currentUrl = webView.url.toString();
                // Strictly require being on music.youtube.com domain, NOT on accounts.google.com!
                if (currentUrl.indexOf("https://music.youtube.com") === 0 || currentUrl.indexOf("http://music.youtube.com") === 0) {
                    var ytCode = `
                        (function() {
                            if (window.location.hostname !== "music.youtube.com") return "";

                            // Ensure YouTube Music SPA config is loaded and user is authenticated
                            if (!window.ytcfg || typeof window.ytcfg.get !== 'function') {
                                return "";
                            }
                            if (window.ytcfg.get('LOGGED_IN') !== true) {
                                return "";
                            }

                            var cookies = document.cookie;
                            if (!cookies) return "";

                            // SAPISID is strictly set only on authenticated sessions
                            var hasSapisid = cookies.indexOf('SAPISID=') !== -1 || cookies.indexOf('__Secure-1PAPISID=') !== -1;
                            if (!hasSapisid) return "";

                            return cookies;
                        })();
                    `;
                    webView.runJavaScript(ytCode, function(result) {
                        if (result && result !== "null" && result !== "") {
                            console.log("[QML] YouTube: Authorization detected, cookies intercepted.");
                            cppAuthManager.onYtAuthIntercepted(result);
                            universalSniper.running = false;
                        }
                    });
                }
            }
        }
    }
}