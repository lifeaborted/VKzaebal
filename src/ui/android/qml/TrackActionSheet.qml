import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    x: 0
    y: parent ? (parent.height - height) : 0
    width: parent ? parent.width : 360
    height: Math.min(480, parent ? parent.height * 0.75 : 480)
    padding: 0
    z: 1000
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property int trackIndex: -1
    property string trackTitle: ""
    property string trackArtist: ""
    property bool isLibrary: false

    signal playRequested(int index)
    signal favoriteToggled(int index, bool isLibrary)
    signal downloadRequested(int index)
    signal queueToggled(int index)
    signal addToPlaylistRequested(int index)
    signal goToAlbumRequested(int index, string title, string artist)

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
        id: contentCol
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

        // Инфо о треке в шапке попапа
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            Layout.bottomMargin: 6
            spacing: 2

            Text {
                text: root.trackTitle !== "" ? root.trackTitle : "Трек"
                font.pixelSize: 15
                font.bold: true
                color: root.colTextPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Text {
                text: root.trackArtist !== "" ? root.trackArtist : "Неизвестный исполнитель"
                font.pixelSize: 12
                color: root.colTextSecondary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.colBorderInner
            Layout.bottomMargin: 4
        }

        // 1. Воспроизвести сейчас
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actPlayM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon { name: "play"; size: 16; color: root.colAccentGreen }
                Text { text: "Воспроизвести сейчас"; font.pixelSize: 13; color: root.colTextPrimary }
            }

            MouseArea {
                id: actPlayM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.playRequested(root.trackIndex)
                }
            }
        }

        // 2. Лайк / Избранное
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actFavM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon {
                    name: root.isLibrary ? "heart-fill" : "heart"
                    size: 16
                    color: root.isLibrary ? root.colAccentGreen : root.colTextSecondary
                }
                Text {
                    text: root.isLibrary ? "Удалить из «Избранное»" : "Добавить в «Избранное»"
                    font.pixelSize: 13
                    color: root.colTextPrimary
                }
            }

            MouseArea {
                id: actFavM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.favoriteToggled(root.trackIndex, root.isLibrary)
                }
            }
        }

        // 3. Скачать трек
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actDlM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon { name: "download"; size: 16; color: root.colTextSecondary }
                Text { text: "Скачать трек на устройство"; font.pixelSize: 13; color: root.colTextPrimary }
            }

            MouseArea {
                id: actDlM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.downloadRequested(root.trackIndex)
                }
            }
        }

        // 4. Очередь
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actQueueM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon {
                    name: "queue"
                    size: 16
                    color: player.isTrackInQueue(root.trackIndex) ? root.colAccentGreen : root.colTextSecondary
                }
                Text {
                    text: player.isTrackInQueue(root.trackIndex) ? "Убрать из очереди" : "Добавить в очередь"
                    font.pixelSize: 13
                    color: root.colTextPrimary
                }
            }

            MouseArea {
                id: actQueueM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.queueToggled(root.trackIndex)
                }
            }
        }

        // 5. Добавить в плейлист
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actPlM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon { name: "plus"; size: 16; color: root.colTextSecondary }
                Text { text: "Добавить в плейлист..."; font.pixelSize: 13; color: root.colTextPrimary }
            }

            MouseArea {
                id: actPlM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.addToPlaylistRequested(root.trackIndex)
                }
            }
        }

        // 6. Перейти к альбому релиза
        Rectangle {
            Layout.fillWidth: true
            height: 44
            radius: 8
            color: actAlbM.pressed ? root.colBgHighlight : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                spacing: 12
                ArgonIcon { name: "disc"; size: 16; color: root.colTextSecondary }
                Text { text: "Перейти к альбому релиза"; font.pixelSize: 13; color: root.colTextPrimary }
            }

            MouseArea {
                id: actAlbM
                anchors.fill: parent
                onClicked: {
                    root.close()
                    root.goToAlbumRequested(root.trackIndex, root.trackTitle, root.trackArtist)
                }
            }
        }

        // 7. Отмена
        Rectangle {
            Layout.fillWidth: true
            height: 42
            radius: 8
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
