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
        id: scSniper
        interval: 2000
        running: cppAuthUrl.indexOf("soundcloud.com") !== -1
        repeat: true
        onTriggered: {
            var jsCode = `
                (function() {
                    // 1. Проверяем Local Storage
                    var token = window.localStorage.getItem('oauth_token');
                    if (token) return token;

                    // 2. Проверяем Cookies
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

            webView.runJavaScript(jsCode, function(result) {
                if (result && result !== "null" && result !== "") {
                    console.log("[QML] SoundCloud Token intercepted via Super-Sniper: " + result);
                    cppAuthManager.onScTokenIntercepted(result);
                    scSniper.running = false;
                }
            });
        }
    }
}