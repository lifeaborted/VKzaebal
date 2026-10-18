import QtQuick
import QtQuick.Layouts

Item {
    id: root
    anchors.horizontalCenter: parent.horizontalCenter
    width: pillRect.width
    height: pillRect.height
    z: 5000

    property string message: ""
    property bool isShown: false

    function show(text) {
        message = text
        isShown = true
        dismissTimer.restart()
    }

    Timer {
        id: dismissTimer
        interval: 2500
        onTriggered: root.isShown = false
    }

    Rectangle {
        id: pillRect
        anchors.centerIn: parent
        height: 36
        width: Math.min(toastRow.implicitWidth + 32, root.parent ? root.parent.width - 48 : 320)
        radius: 18
        color: "#E616161D"
        border.color: "#26FFFFFF"
        opacity: root.isShown ? 1.0 : 0.0
        scale: root.isShown ? 1.0 : 0.9

        Behavior on opacity {
            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
        }
        Behavior on scale {
            NumberAnimation { duration: 200; easing.type: Easing.OutBack }
        }

        RowLayout {
            id: toastRow
            anchors.centerIn: parent
            spacing: 8

            ArgonIcon {
                name: "check"
                size: 13
                color: "#47B881"
            }

            Text {
                text: root.message
                font.pixelSize: 12
                font.bold: true
                color: "#FFFFFF"
                elide: Text.ElideRight
            }
        }
    }
}
