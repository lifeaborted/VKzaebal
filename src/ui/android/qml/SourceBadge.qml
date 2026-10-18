import QtQuick

Item {
    id: root
    property string sourceName: "VK"
    property real badgeSize: 14

    width: badgeSize
    height: badgeSize

    readonly property string iconPath: {
        var s = (sourceName || "").toUpperCase()
        if (s.indexOf("VK") !== -1) return "qrc:/ui/android/assets/icons/services/vk.png"
        if (s.indexOf("SOUNDCLOUD") !== -1 || s === "SC") return "qrc:/ui/android/assets/icons/services/soundcloud.png"
        if (s.indexOf("YANDEX") !== -1 || s === "YA") return "qrc:/ui/android/assets/icons/services/yandex.png"
        if (s.indexOf("YOUTUBE") !== -1 || s === "YT") return "qrc:/ui/android/assets/icons/services/youtube.png"
        if (s.indexOf("OFFLINE") !== -1 || s === "DL" || s === "СКАЧАННЫЕ") return "qrc:/ui/android/assets/icons/services/offline.png"
        return "qrc:/ui/android/assets/icons/services/all.png"
    }

    Image {
        id: iconImg
        anchors.fill: parent
        source: root.iconPath
        sourceSize: Qt.size(root.badgeSize * 2, root.badgeSize * 2)
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        visible: status === Image.Ready
    }

    // Текстовый фоллбэк если картинка не загрузилась
    Rectangle {
        anchors.fill: parent
        visible: !iconImg.visible
        radius: 3
        color: {
            var s = (root.sourceName || "").toUpperCase()
            if (s.indexOf("VK") !== -1) return "#0077FF"
            if (s.indexOf("SOUNDCLOUD") !== -1 || s === "SC") return "#FF5500"
            if (s.indexOf("YANDEX") !== -1 || s === "YA") return "#FC3F1D"
            if (s.indexOf("YOUTUBE") !== -1 || s === "YT") return "#FF0000"
            if (s.indexOf("OFFLINE") !== -1 || s === "DL" || s === "СКАЧАННЫЕ") return "#47B881"
            return "#5C5C66"
        }

        Text {
            anchors.centerIn: parent
            text: {
                var s = (root.sourceName || "").toUpperCase()
                if (s.indexOf("VK") !== -1) return "VK"
                if (s.indexOf("SOUNDCLOUD") !== -1 || s === "SC") return "SC"
                if (s.indexOf("YANDEX") !== -1 || s === "YA") return "YA"
                if (s.indexOf("YOUTUBE") !== -1 || s === "YT") return "YT"
                if (s.indexOf("OFFLINE") !== -1 || s === "DL" || s === "СКАЧАННЫЕ") return "DL"
                return "ALL"
            }
            color: "#FFFFFF"
            font.pixelSize: Math.max(7, Math.round(root.badgeSize * 0.65))
            font.bold: true
        }
    }
}
