import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    x: 0
    y: parent ? (parent.height - height) : 0
    width: parent ? parent.width : 360
    height: Math.min(290, parent ? parent.height * 0.5 : 290)
    padding: 0
    z: 1000
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    signal sortSelected(string mode, string label)

    readonly property color colBgSurface: "#15151C"
    readonly property color colBgHighlight: "#1F1F28"
    readonly property color colBorderShell: "#14FFFFFF"
    readonly property color colBorderInner: "#0DFFFFFF"
    readonly property color colAccentGreen: "#47B881"
    readonly property color colTextPrimary: "#FFFFFF"
    readonly property color colTextSecondary: "#9E9EA8"

    background: Rectangle {
        color: root.colBgSurface
        radius: 20
        border.color: root.colBorderShell
    }

    enter: Transition {
        NumberAnimation { property: "y"; from: root.parent ? root.parent.height : 800; to: root.parent ? (root.parent.height - root.height) : 0; duration: 200; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "y"; from: root.parent ? (root.parent.height - root.height) : 0; to: root.parent ? root.parent.height : 800; duration: 180; easing.type: Easing.InCubic }
    }

    contentItem: ColumnLayout {
        id: sortCol
        anchors.fill: parent
        anchors.margins: 16
        spacing: 6

        // Handle
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 36
            height: 4
            radius: 2
            color: "#26FFFFFF"
            Layout.bottomMargin: 4
        }

        Text {
            text: "Сортировка треков"
            font.pixelSize: 15
            font.bold: true
            color: root.colTextPrimary
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.bottomMargin: 6
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.colBorderInner
            Layout.bottomMargin: 4
        }

        Repeater {
            model: [
                { mode: "date-desc", label: "По дате добавления (новые)" },
                { mode: "date-asc", label: "По дате добавления (старые)" },
                { mode: "title-asc", label: "По названию (А → Я)" },
                { mode: "artist-asc", label: "По исполнителю (А → Я)" }
            ]

            delegate: Rectangle {
                Layout.fillWidth: true
                height: 44
                radius: 10
                color: optMouse.pressed ? root.colBgHighlight : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    ArgonIcon {
                        name: "sort"
                        size: 16
                        color: root.colTextSecondary
                    }

                    Text {
                        text: modelData.label
                        font.pixelSize: 13
                        font.bold: true
                        color: root.colTextPrimary
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: optMouse
                    anchors.fill: parent
                    onClicked: {
                        root.sortSelected(modelData.mode, modelData.label)
                        root.close()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.colBorderInner
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }

        Rectangle {
            Layout.fillWidth: true
            height: 42
            radius: 10
            color: "#0DFFFFFF"

            Text {
                anchors.centerIn: parent
                text: "Отмена"
                font.pixelSize: 13
                font.bold: true
                color: root.colTextSecondary
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.close()
            }
        }
    }
}
