import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    width: parent.width
    height: parent.height
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#141415"
    }

    enter: Transition {
        NumberAnimation { property: "y"; from: root.height; to: 0; duration: 250; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "y"; from: 0; to: root.height; duration: 200; easing.type: Easing.InCubic }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Верхний бар с кнопкой закрытия
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10

            ToolButton {
                text: "▼"
                font.pixelSize: 16
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 16
                    color: "#E1E3E6"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
                onClicked: root.close()
            }

            Text {
                text: "Сейчас играет"
                font.pixelSize: 16
                font.bold: true
                color: "#909499"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Item { width: 40; height: 1 } // Балансир
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: 10 }

        // Большая обложка
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(root.width - 60, 300)
            Layout.preferredHeight: Layout.preferredWidth
            radius: 16
            color: "#262628"
            clip: true

            Image {
                anchors.fill: parent
                source: player.currentCoverUrl
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: player.currentCoverUrl !== "" && status === Image.Ready
            }

            Text {
                anchors.centerIn: parent
                text: "🎵"
                font.pixelSize: 64
                visible: player.currentCoverUrl === "" || !parent.children[0].visible
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: 10 }

        // Название и артист
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: player.currentTitle
                font.pixelSize: 20
                font.bold: true
                color: "#FFFFFF"
                Layout.fillWidth: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: player.currentArtist !== "" ? player.currentArtist : "Неизвестный исполнитель"
                font.pixelSize: 15
                color: "#909499"
                Layout.fillWidth: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: "[" + player.currentSource.toUpperCase() + "]"
                font.pixelSize: 12
                font.bold: true
                color: "#2688EB"
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // Ползунок перемотки
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.durationMs)
                value: pressed ? value : player.positionMs

                onMoved: {
                    player.seek(value)
                }

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - 2
                    width: seekSlider.availableWidth
                    height: 4
                    radius: 2
                    color: "#333336"

                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        color: "#2688EB"
                        radius: 2
                    }
                }

                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: "#FFFFFF"
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: player.formattedPosition
                    font.pixelSize: 12
                    color: "#76787A"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: player.formattedDuration
                    font.pixelSize: 12
                    color: "#76787A"
                }
            }
        }

        // Кнопки управления воспроизведением
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 24

            ToolButton {
                text: "⏮"
                font.pixelSize: 26
                contentItem: Text { text: parent.text; font.pixelSize: 26; color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "transparent" }
                onClicked: player.prevTrack()
            }

            Rectangle {
                width: 64
                height: 64
                radius: 32
                color: "#2688EB"

                Text {
                    anchors.centerIn: parent
                    text: player.isPlaying ? "⏸" : "▶"
                    font.pixelSize: 26
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.togglePlay()
                }
            }

            ToolButton {
                text: "⏭"
                font.pixelSize: 26
                contentItem: Text { text: parent.text; font.pixelSize: 26; color: "#FFFFFF"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { color: "transparent" }
                onClicked: player.nextTrack()
            }
        }

        // Громкость
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            spacing: 12

            Text { text: "🔈"; font.pixelSize: 16; color: "#909499" }

            Slider {
                id: volSlider
                Layout.fillWidth: true
                from: 0.0
                to: 1.0
                value: player.volume
                onMoved: player.setVolume(value)

                background: Rectangle {
                    x: volSlider.leftPadding
                    y: volSlider.topPadding + volSlider.availableHeight / 2 - 2
                    width: volSlider.availableWidth
                    height: 4
                    radius: 2
                    color: "#333336"

                    Rectangle {
                        width: volSlider.visualPosition * parent.width
                        height: parent.height
                        color: "#909499"
                        radius: 2
                    }
                }

                handle: Rectangle {
                    x: volSlider.leftPadding + volSlider.visualPosition * (volSlider.availableWidth - width)
                    y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: "#FFFFFF"
                }
            }

            Text { text: "🔊"; font.pixelSize: 16; color: "#909499" }
        }
    }
}
