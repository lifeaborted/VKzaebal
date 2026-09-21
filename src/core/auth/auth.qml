import QtQuick
import QtQuick.Window
import QtWebView
import QtQml

Window {
    id: authWindow
    width: 900
    height: 700
    visible: (cppAuthUrl.indexOf("oauth.vk.com") === -1 && cppAuthUrl.indexOf("oauth.vk.ru") === -1 && cppAuthUrl.indexOf("id.vk.com") === -1)
    title: "Авторизация"

    Timer {
        id: showWindowTimer
        interval: 12000
        running: !authWindow.visible
        repeat: false
        onTriggered: {
            authWindow.visible = true
        }
    }

    WebView {
        id: webView
        anchors.fill: parent
        url: cppAuthUrl

        onUrlChanged: {
            console.log("[QML] Текущий URL: " + url.toString())
            cppAuthManager.onUrlIntercepted(url.toString())
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
                        // 1. Автоматически нажимаем кнопку подтверждения прав ("Разрешить" / "Продолжить")
                        var btn = document.querySelector('.oauth_button .flat_button') ||
                                  document.querySelector('button.flat_button[type="submit"]') ||
                                  document.querySelector('.oauth_button') ||
                                  document.querySelector('button[type="submit"]') ||
                                  document.querySelector('.vkc__Button__primary') ||
                                  document.querySelector('button.vkuiButton--mode-primary') ||
                                  document.querySelector('[data-test-id="continue-as-button"]') ||
                                  document.querySelector('[data-test-id="verification-continue-button"]');
                        if (btn && !btn.disabled) {
                            btn.click();
                            return "approved";
                        }

                        // 2. Проверяем, есть ли РЕАЛЬНО ВИДИМЫЕ поля ввода логина/пароля/кода
                        function isVisible(el) {
                            if (!el) return false;
                            var rect = el.getBoundingClientRect();
                            if (rect.width === 0 || rect.height === 0) return false;
                            var style = window.getComputedStyle(el);
                            return style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0';
                        }

                        var inputs = document.querySelectorAll('input[type="password"], input[name="login"], input[type="tel"], input[autocomplete="one-time-code"]');
                        for (var i = 0; i < inputs.length; i++) {
                            if (isVisible(inputs[i])) {
                                return "show_window";
                            }
                        }

                        return "waiting";
                    })();
                `;
                webView.runJavaScript(vkCode, function(result) {
                    if (result === "show_window") {
                        authWindow.visible = true;
                    } else if (result === "approved") {
                        console.log("[QML] VK: Authorization consent approved automatically.");
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