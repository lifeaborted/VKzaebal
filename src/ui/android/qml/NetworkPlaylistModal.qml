import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    x: parent ? ((parent.width - width) / 2) : 0
    y: parent ? ((parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width * 0.88, 360) : 340
    height: dialogCol.implicitHeight + 36
    modal: true
    focus: true
    padding: 0
    z: 1000

    signal importRequested(string url)

    readonly property color colBgSurface: "#16161D"
    readonly property color colBgHighlight: "#1F1F28"
    readonly property color colBorderShell: "#14FFFFFF"
    readonly property color colAccentGreen: "#47B881"
    readonly property color colTextPrimary: "#FFFFFF"
    readonly property color colTextSecondary: "#9E9EA8"
    readonly property color colTextSubdued: "#5C5C66"

    background: Rectangle {
        color: root.colBgSurface
        radius: 16
        border.color: root.colBorderShell
    }

    contentItem: ColumnLayout {
        id: dialogCol
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Text {
            text: "Импорт плейлиста"
            font.pixelSize: 17
            font.bold: true
            color: root.colTextPrimary
        }

        Text {
            text: "Вставьте ссылку на плейлист VK, SoundCloud, Яндекс или YouTube Music"
            font.pixelSize: 12
            color: root.colTextSecondary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 42
            radius: 8
            color: root.colBgHighlight
            border.color: root.colBorderShell

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                ArgonIcon {
                    name: "link"
                    size: 16
                    color: root.colTextSubdued
                }

                TextField {
                    id: urlInput
                    Layout.fillWidth: true
                    placeholderText: "https://music.youtube.com/playlist?list=..."
                    placeholderTextColor: root.colTextSubdued
                    color: root.colTextPrimary
                    font.pixelSize: 12
                    background: Item {}
                    onAccepted: {
                        if (text.trim() !== "") {
                            root.importRequested(text.trim())
                            text = ""
                            root.close()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 90
                height: 34
                radius: 17
                color: "#14FFFFFF"

                Text {
                    anchors.centerIn: parent
                    text: "Отмена"
                    font.pixelSize: 12
                    font.bold: true
                    color: root.colTextSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        urlInput.text = ""
                        root.close()
                    }
                }
            }

            Rectangle {
                width: 120
                height: 34
                radius: 17
                color: root.colAccentGreen

                Text {
                    anchors.centerIn: parent
                    text: "Импортировать"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#09090C"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (urlInput.text.trim() !== "") {
                            root.importRequested(urlInput.text.trim())
                            urlInput.text = ""
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
