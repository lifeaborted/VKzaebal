import QtQuick
import QtQuick.Window
import QtWebView

Window {
    width: 900
    height: 700
    visible: true
    title: "Авторизация ВКонтакте"

    WebView {
        anchors.fill: parent
        url: cppAuthUrl

        onUrlChanged: {
            console.log("[QML] Текущий URL: " + url.toString())
            cppAuthManager.onUrlIntercepted(url.toString())
        }
    }
}