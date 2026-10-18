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
        color: "#09090C"

        // Тонкий мягкий градиент сверху
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 280
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#161622" }
                GradientStop { position: 1.0; color: "#09090C" }
            }
        }
    }

    enter: Transition {
        NumberAnimation { property: "y"; from: root.height; to: 0; duration: 250; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "y"; from: 0; to: root.height; duration: 200; easing.type: Easing.InCubic }
    }

    signal openEqualizer()
    signal openMoreActions()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 12
        anchors.bottomMargin: 24
        spacing: 12

        // Верхний бар с кнопкой свернуть, заголовком и кнопкой опций
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: "#16161D"
                border.color: "#1AFFFFFF"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "chevron-down"
                    size: 16
                    color: "#9E9EA8"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.close()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: "СЕЙЧАС ИГРАЕТ"
                    font.pixelSize: 10
                    font.bold: true
                    color: "#5C5C66"
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }

                Text {
                    text: player.currentTrackSource
                    font.pixelSize: 12
                    font.bold: true
                    color: "#9E9EA8"
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: "#16161D"
                border.color: "#1AFFFFFF"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "more"
                    size: 16
                    color: "#9E9EA8"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.openMoreActions()
                }
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: 6 }

        // Большая обложка альбома с скруглением
        Rectangle {
            id: coverContainer
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(root.width - 64, 310)
            Layout.preferredHeight: Layout.preferredWidth
            radius: 16
            color: "#16161D"
            border.color: "#14FFFFFF"
            clip: true

            Image {
                id: coverImg
                anchors.fill: parent
                source: player.currentCoverUrl
                sourceSize: Qt.size(600, 600)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: player.currentCoverUrl !== "" && status === Image.Ready
            }

            // Плейсхолдер винила если нет обложки
            Item {
                anchors.centerIn: parent
                visible: !coverImg.visible
                width: 80
                height: 80

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "disc"
                    size: 72
                    color: "#2C2C35"
                }
            }

            // Микро-бейдж источника в углу обложки
            SourceBadge {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 10
                sourceName: player.currentTrackSource
                badgeSize: 16
            }
        }

        Item { Layout.fillHeight: true; Layout.preferredHeight: 6 }

        // Строка: Название трека + Артист + Лайк
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    text: player.currentTitle
                    font.pixelSize: 18
                    font.bold: true
                    color: "#FFFFFF"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: player.currentArtist !== "" ? player.currentArtist : "Неизвестный исполнитель"
                    font.pixelSize: 14
                    color: "#9E9EA8"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            // Кнопка лайка (heart / heart-fill)
            Rectangle {
                width: 40
                height: 40
                radius: 20
                color: player.isCurrentFavorite ? "#1F47B881" : "#16161D"
                border.color: player.isCurrentFavorite ? "#47B881" : "#1AFFFFFF"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: player.isCurrentFavorite ? "heart-fill" : "heart"
                    size: 18
                    color: player.isCurrentFavorite ? "#47B881" : "#9E9EA8"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.toggleCurrentFavorite()
                }
            }
        }

        // Ползунок перемотки (SeekBar)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            function formatTime(ms) {
                var totalSec = Math.floor(Math.max(0, ms) / 1000)
                var m = Math.floor(totalSec / 60)
                var s = totalSec % 60
                return m + ":" + (s < 10 ? "0" : "") + s
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, player.durationMs)
                value: pressed ? value : player.positionMs

                onPressedChanged: {
                    if (!pressed) {
                        player.seek(value)
                    }
                }

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - 2
                    width: seekSlider.availableWidth
                    height: 4
                    radius: 2
                    color: "#1F1F28"

                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        color: "#47B881"
                        radius: 2
                    }
                }

                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.pressed ? 14 : 10
                    height: width
                    radius: width / 2
                    color: "#FFFFFF"
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: seekSlider.pressed ? parent.formatTime(seekSlider.value) : player.formattedPosition
                    font.pixelSize: 11
                    font.family: "JetBrains Mono"
                    color: "#5C5C66"
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: player.formattedDuration
                    font.pixelSize: 11
                    font.family: "JetBrains Mono"
                    color: "#5C5C66"
                }
            }
        }

        // Кнопки управления воспроизведением (Shuffle, Prev, Play/Pause, Next, Repeat)
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 20
            Layout.topMargin: 4
            Layout.bottomMargin: 4

            // Шафл
            Rectangle {
                width: 38
                height: 38
                radius: 19
                color: player.isShuffle ? "#1F47B881" : "transparent"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "shuffle"
                    size: 18
                    color: player.isShuffle ? "#47B881" : "#5C5C66"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.setShuffle(!player.isShuffle)
                }
            }

            // Предыдущий трек
            Rectangle {
                width: 44
                height: 44
                radius: 22
                color: "transparent"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "prev"
                    size: 22
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.prevTrack()
                }
            }

            // Play / Pause (крупная круглая кнопка со студийным акцентом)
            Rectangle {
                width: 62
                height: 62
                radius: 31
                color: "#47B881"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: player.isPlaying ? "pause" : "play"
                    size: 24
                    color: "#09090C"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.togglePlay()
                }
            }

            // Следующий трек
            Rectangle {
                width: 44
                height: 44
                radius: 22
                color: "transparent"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "next"
                    size: 22
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.nextTrack()
                }
            }

            // Повтор
            Rectangle {
                width: 38
                height: 38
                radius: 19
                color: player.repeatMode !== 0 ? "#1F47B881" : "transparent"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "repeat"
                    size: 18
                    color: player.repeatMode !== 0 ? "#47B881" : "#5C5C66"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: player.toggleRepeat()
                }
            }
        }

        // Полоса текста песни (Lyrics Banner в стиле Spotify / Argon)
        Rectangle {
            Layout.fillWidth: true
            height: 48
            radius: 14
            color: "#16161D"
            border.color: "#14FFFFFF"
            Layout.topMargin: 12
            Layout.bottomMargin: 8

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                ArgonIcon {
                    name: "lyrics"
                    size: 16
                    color: "#47B881"
                }

                Text {
                    text: "Текст песни (Lyrics)"
                    font.pixelSize: 13
                    font.bold: true
                    color: "#FFFFFF"
                    Layout.fillWidth: true
                }

                Text {
                    text: player.currentLyrics !== "" ? "[ТЕКСТ ЗАГРУЖЕН]" : "[ОТКРЫТЬ]"
                    font.pixelSize: 11
                    font.family: "JetBrains Mono"
                    font.bold: true
                    color: "#47B881"
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    lyricsPopup.open()
                    if (player.currentLyrics === "") {
                        player.fetchLyrics()
                    }
                }
            }
        }
    }

    // =========================================================================
    // Всплывающее окно текста песни (Lyrics Sheet)
    // =========================================================================
    Popup {
        id: lyricsPopup
        width: parent.width
        height: Math.min(parent.height * 0.82, 600)
        y: parent.height - height
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#111116"
            radius: 20
            border.color: "#1AFFFFFF"
        }

        enter: Transition {
            NumberAnimation { property: "y"; from: root.height; to: root.height - lyricsPopup.height; duration: 250; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "y"; from: root.height - lyricsPopup.height; to: root.height; duration: 200; easing.type: Easing.InCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Текст песни"
                    font.pixelSize: 18
                    font.bold: true
                    color: "#FFFFFF"
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: "#16161D"
                    border.color: "#1AFFFFFF"

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: "close"
                        size: 14
                        color: "#9E9EA8"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: lyricsPopup.close()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: player.currentTitle
                    font.pixelSize: 14
                    font.bold: true
                    color: "#FFFFFF"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: player.currentArtist
                    font.pixelSize: 12
                    color: "#9E9EA8"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#14FFFFFF"
            }

            Flickable {
                id: lyricsFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: lyricsText.implicitHeight + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { active: true }

                Text {
                    id: lyricsText
                    width: lyricsFlick.width
                    text: player.currentLyrics !== "" ? player.currentLyrics : "Текст загружается или не найден..."
                    font.pixelSize: 15
                    lineHeight: 1.5
                    color: player.currentLyrics !== "" ? "#FFFFFF" : "#5C5C66"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
