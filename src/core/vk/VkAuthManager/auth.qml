import QtQuick
import QtWebView

Item {
    width: 400
    height: 600

    WebView {
        anchors.fill: parent
        url: cppAuthUrl

        onUrlChanged: {
            cppAuthManager.onUrlIntercepted(url.toString())
        }
    }
}