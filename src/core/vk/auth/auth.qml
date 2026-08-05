import QtQuick
import QtQuick.Window
import QtWebView

Window {
    width: 400
    height: 600
    visible: true
    title: "Авторизация ВКонтакте"

    WebView {
        anchors.fill: parent
        url: cppAuthUrl

        onUrlChanged: {
            cppAuthManager.onUrlIntercepted(url.toString())
        }
    }
}