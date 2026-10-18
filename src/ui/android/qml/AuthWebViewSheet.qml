import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebView

Rectangle {
    id: webViewSheet
    anchors.fill: parent
    color: "#18181A"
    visible: player.isWebViewAuthOpen
    z: 200

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // =====================================================================
        // Заголовок шторки WebView
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            height: 54
            color: "#212124"
            border.color: "#303034"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: closeArea.pressed ? "#36363B" : "#2A2A2E"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 18
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        onClicked: player.closeWebViewAuth()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "Авторизация: " + player.webViewAuthService
                        font.pixelSize: 15
                        font.bold: true
                        color: "#FFFFFF"
                    }

                    Text {
                        text: webView.loading ? "Загрузка страницы..." : "Войдите в свою учетную запись"
                        font.pixelSize: 11
                        color: "#909499"
                    }
                }

                // Индикатор загрузки страницы
                BusyIndicator {
                    running: webView.loading
                    visible: webView.loading
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                }
            }
        }

        // Тонкий прогресс-бар загрузки
        Rectangle {
            Layout.fillWidth: true
            height: 2
            color: "#2688EB"
            visible: webView.loading
            opacity: 0.8
        }

        // =====================================================================
        // Нативный WebView контейнер
        // =====================================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            WebView {
                id: webView
                anchors.fill: parent
                url: player.webViewAuthUrl

                onUrlChanged: {
                    var currentStr = url.toString()
                    console.log("[WebView Auth] URL: " + currentStr)
                    player.onWebViewUrlChanged(currentStr)
                }

                onLoadingChanged: function(loadRequest) {
                    console.log("[WebView Auth] Status: " + loadRequest.status + " url=" + loadRequest.url)
                }
            }
        }
    }

    // =========================================================================
    // JS-снайпер перехвата токенов сессии (SoundCloud & YouTube Music)
    // =========================================================================
    Timer {
        id: sniperTimer
        interval: 400
        running: player.isWebViewAuthOpen
        repeat: true
        onTriggered: {
            var service = player.webViewAuthService.toLowerCase()

            // --- SoundCloud Sniper ---
            if (service === "soundcloud") {
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
                        console.log("[QML Sniper] SoundCloud Token intercepted!");
                        player.onScTokenIntercepted(result)
                    }
                })
            }

            // --- YouTube Music Sniper ---
            else if (service === "youtube") {
                var currentUrl = webView.url.toString()
                if (currentUrl.indexOf("https://music.youtube.com") === 0 || currentUrl.indexOf("http://music.youtube.com") === 0) {
                    var ytCode = `
                        (function() {
                            if (window.location.hostname !== "music.youtube.com") return "";
                            if (!window.ytcfg || typeof window.ytcfg.get !== 'function') return "";
                            if (window.ytcfg.get('LOGGED_IN') !== true) return "";

                            var cookies = document.cookie;
                            if (!cookies) return "";
                            var hasSapisid = cookies.indexOf('SAPISID=') !== -1 || cookies.indexOf('__Secure-1PAPISID=') !== -1;
                            if (!hasSapisid) return "";
                            return cookies;
                        })();
                    `;
                    webView.runJavaScript(ytCode, function(result) {
                        if (result && result !== "null" && result !== "") {
                            console.log("[QML Sniper] YouTube session intercepted!")
                            player.onYtAuthIntercepted(result)
                        }
                    })
                }
            }
        }
    }
}
