import QtQuick
import QtQuick.Window
import QtWebView
import QtQml

Window {
    width: 900
    height: 700
    visible: true
    title: "Авторизация"

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
            else if (cppAuthUrl.indexOf("oauth.vk.com") !== -1) {
                var vkCode = `
                    (function() {
                        var btn = document.querySelector('button[type="submit"]') || document.querySelector('.oauth_button .flat_button');
                        if (btn && !btn.disabled) {
                            btn.click();
                            return true;
                        }
                        return false;
                    })();
                `;
                webView.runJavaScript(vkCode, function(result) {
                    if (result === true) {
                        console.log("[QML] VK: Authorization.");
                    }
                });
            }
        }
    }
}