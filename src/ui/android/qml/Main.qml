import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 420
    height: 750
    title: "VKAudioPlayer"
    color: "#19191A"

    // =========================================================================
    // Боковое меню источников (Drawer)
    // =========================================================================
    Drawer {
        id: sourceDrawer
        width: Math.min(window.width * 0.78, 300)
        height: window.height
        edge: Qt.LeftEdge

        background: Rectangle {
            color: "#212124"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                spacing: 10
                Layout.topMargin: 20
                Text { text: "🎧"; font.pixelSize: 28 }
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "VKAudioPlayer"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#FFFFFF"
                    }
                    Text {
                        text: "v2.0 (Mobile Android)"
                        font.pixelSize: 12
                        color: "#909499"
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#303033"
                Layout.topMargin: 8
                Layout.bottomMargin: 8
            }

            Text {
                text: "ИСТОЧНИКИ МУЗЫКИ"
                font.pixelSize: 11
                font.bold: true
                color: "#76787A"
            }

            Repeater {
                model: [
                    { id: "vk", name: "ВКонтакте", icon: "🔵" },
                    { id: "youtube", name: "YouTube Music", icon: "🔴" },
                    { id: "soundcloud", name: "SoundCloud", icon: "🟠" },
                    { id: "yandex", name: "Яндекс Музыка", icon: "🟡" },
                    { id: "spotify", name: "Spotify", icon: "🟢" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 48
                    radius: 10
                    color: player.currentSource === modelData.id ? "#2C394B" : (itemMouse.containsMouse ? "#29292D" : "transparent")

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        Text { text: modelData.icon; font.pixelSize: 18 }

                        Text {
                            text: modelData.name
                            font.pixelSize: 15
                            font.bold: player.currentSource === modelData.id
                            color: player.currentSource === modelData.id ? "#2688EB" : "#E1E3E6"
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "✓"
                            font.pixelSize: 16
                            color: "#2688EB"
                            visible: player.currentSource === modelData.id
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            player.switchSource(modelData.id)
                            sourceDrawer.close()
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Инфо о текущем статусе
            Rectangle {
                Layout.fillWidth: true
                height: 60
                radius: 8
                color: "#18181A"
                border.color: "#2A2A2D"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text { text: "Статус плеера:"; font.pixelSize: 11; color: "#76787A" }
                    Text {
                        text: player.statusMessage !== "" ? player.statusMessage : "Готов к воспроизведению"
                        font.pixelSize: 11
                        color: "#909499"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // =========================================================================
    // Основной лейаут
    // =========================================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- Верхняя поисковая панель (Header в стиле VK) ---
        Rectangle {
            Layout.fillWidth: true
            height: 105
            color: "#212124"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ToolButton {
                        text: "☰"
                        font.pixelSize: 22
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 22
                            color: "#E1E3E6"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: "transparent" }
                        onClicked: sourceDrawer.open()
                    }

                    // Поле ввода поиска
                    Rectangle {
                        Layout.fillWidth: true
                        height: 38
                        radius: 8
                        color: "#2E2E32"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 6

                            Text { text: "🔍"; font.pixelSize: 14; color: "#76787A" }

                            TextField {
                                id: searchInput
                                Layout.fillWidth: true
                                placeholderText: "Поиск музыки..."
                                placeholderTextColor: "#76787A"
                                color: "#FFFFFF"
                                font.pixelSize: 14
                                background: Item {}
                                onAccepted: {
                                    player.search(text)
                                }
                            }

                            ToolButton {
                                visible: searchInput.text !== ""
                                text: "✕"
                                font.pixelSize: 12
                                contentItem: Text { text: parent.text; color: "#909499"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { color: "transparent" }
                                onClicked: {
                                    searchInput.text = ""
                                }
                            }
                        }
                    }

                    // Кнопка запуска поиска
                    Rectangle {
                        width: 38
                        height: 38
                        radius: 8
                        color: "#2688EB"

                        Text {
                            anchors.centerIn: parent
                            text: "➔"
                            font.pixelSize: 16
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: player.search(searchInput.text)
                        }
                    }
                }

                // Табы: "Мои треки" / "Результаты поиска"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        Layout.preferredWidth: 100
                        height: 28
                        radius: 14
                        color: player.activeTabIndex === 0 ? "#2688EB" : "#2E2E32"

                        Text {
                            anchors.centerIn: parent
                            text: "Мои треки"
                            font.pixelSize: 12
                            font.bold: player.activeTabIndex === 0
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: player.loadMyTracks()
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 110
                        height: 28
                        radius: 14
                        color: player.activeTabIndex === 1 ? "#2688EB" : "#2E2E32"

                        Text {
                            anchors.centerIn: parent
                            text: "Поиск"
                            font.pixelSize: 12
                            font.bold: player.activeTabIndex === 1
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (searchInput.text !== "") {
                                    player.search(searchInput.text)
                                } else {
                                    player.activeTabIndex = 1
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "[" + player.currentSource.toUpperCase() + "]"
                        font.pixelSize: 11
                        font.bold: true
                        color: "#2688EB"
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }
        }

        // --- Индикатор статуса / поиска ---
        Rectangle {
            Layout.fillWidth: true
            height: (player.statusMessage !== "" || player.isSearching) ? 28 : 0
            visible: height > 0
            color: "#1E293B"
            clip: true

            Behavior on height { NumberAnimation { duration: 150 } }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                BusyIndicator {
                    running: player.isSearching
                    visible: player.isSearching
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }

                Text {
                    text: player.statusMessage
                    font.pixelSize: 11
                    color: "#60A5FA"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        // --- Центральный список треков (ListView с обложками) ---
        ListView {
            id: trackListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: trackModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                active: true
            }

            delegate: Rectangle {
                width: trackListView.width
                height: 62
                color: isPlaying ? "#242932" : (trackMouse.containsMouse ? "#202022" : "transparent")

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 66
                    height: 1
                    color: "#232326"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    // Обложка трека
                    Rectangle {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        radius: 6
                        color: "#262628"
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: model.coverUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: model.coverUrl !== "" && status === Image.Ready
                        }

                        Text {
                            anchors.centerIn: parent
                            text: isPlaying ? "▶" : "🎵"
                            font.pixelSize: 18
                            color: isPlaying ? "#2688EB" : "#909499"
                            visible: model.coverUrl === "" || !parent.children[0].visible
                        }
                    }

                    // Название и артист
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: model.title
                            font.pixelSize: 14
                            font.bold: isPlaying
                            color: isPlaying ? "#2688EB" : "#FFFFFF"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: model.artist !== "" ? model.artist : "Неизвестный исполнитель"
                            font.pixelSize: 12
                            color: "#909499"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    // Длительность
                    Text {
                        text: model.durationStr
                        font.pixelSize: 12
                        color: isPlaying ? "#2688EB" : "#76787A"
                    }
                }

                MouseArea {
                    id: trackMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        player.playTrackAtIndex(model.index)
                    }
                }
            }

            // Плейсхолдер пустого списка
            Item {
                anchors.centerIn: parent
                width: 250
                height: 120
                visible: trackListView.count === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: "🎶"
                        font.pixelSize: 40
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: "Список треков пуст"
                        font.pixelSize: 15
                        font.bold: true
                        color: "#FFFFFF"
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: "Введите запрос в поиске выше или выберите источник в боковом меню"
                        font.pixelSize: 12
                        color: "#909499"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 240
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // --- Нижний мини-плеер (Трей) ---
        Rectangle {
            id: miniPlayerTray
            Layout.fillWidth: true
            height: 64
            color: "#212124"

            // Линия прогресса сверху
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                height: 2
                width: player.durationMs > 0 ? (parent.width * (player.positionMs / player.durationMs)) : 0
                color: "#2688EB"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                // Миниатюра обложки
                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "#2B2B2F"
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
                        font.pixelSize: 16
                        visible: player.currentCoverUrl === "" || !parent.children[0].visible
                    }
                }

                // Текст играющего трека
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: player.currentTitle
                        font.pixelSize: 13
                        font.bold: true
                        color: "#FFFFFF"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Text {
                        text: player.currentArtist !== "" ? player.currentArtist : "Нажмите для открытия плеера"
                        font.pixelSize: 11
                        color: "#909499"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                // Кнопка Play / Pause
                Rectangle {
                    width: 38
                    height: 38
                    radius: 19
                    color: "#2688EB"

                    Text {
                        anchors.centerIn: parent
                        text: player.isPlaying ? "⏸" : "▶"
                        font.pixelSize: 16
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.togglePlay()
                    }
                }

                // Кнопка Next
                ToolButton {
                    text: "⏭"
                    font.pixelSize: 18
                    contentItem: Text { text: parent.text; font.pixelSize: 18; color: "#E1E3E6"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: "transparent" }
                    onClicked: player.nextTrack()
                }
            }

            // Тап по трею открывает полноэкранный NowPlayingSheet
            MouseArea {
                anchors.fill: parent
                propagateComposedEvents: true
                z: -1
                onClicked: nowPlayingSheet.open()
            }
        }
    }

    // Полноэкранный модальный плеер
    NowPlayingSheet {
        id: nowPlayingSheet
    }
}
