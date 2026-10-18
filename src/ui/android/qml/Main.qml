import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 420
    height: 750
    title: "VKAudioPlayer"
    color: "#09090C"

    // -------------------------------------------------------------------------
    // Состояние приложения и дизайн-токены (Argon Studio Emerald)
    // -------------------------------------------------------------------------
    property int currentTab: 0 // 0: Главная, 1: Поиск, 2: Распознавание, 3: Медиатека, 4: Настройки
    property string activeSourceFilter: "All"
    property string libraryFilter: "all" // all, custom, saved

    onCurrentTabChanged: {
        if (subScreen.isOpen) subScreen.isOpen = false
    }

    property int selectedTrackIndex: -1
    property string selectedTrackTitle: ""
    property string selectedTrackArtist: ""
    property bool selectedTrackIsLibrary: false

    property var recommendedAlbums: [
        // VK
        { id: "alb-vk-1", title: "БОЖЕ, ХРАНИ КЬЮТ-РОК", artist: "Дора", source: "VK", color: "#2B1A28" },
        { id: "alb-vk-2", title: "ЮНОСТЬ", artist: "СИНТИ ПАНК", source: "VK", color: "#221A30" },
        { id: "alb-vk-3", title: "VK Музыка: Топ 100", artist: "VK Музыка", source: "VK", color: "#1B2A3D" },
        // SoundCloud
        { id: "alb-sc-1", title: "PHUNERAL", artist: "PHARAOH", source: "SoundCloud", color: "#2B1A40" },
        { id: "alb-sc-2", title: "Wave Hits & Drift", artist: "SoundCloud", source: "SoundCloud", color: "#3D1A00" },
        { id: "alb-sc-3", title: "Underground Phonk", artist: "SoundCloud", source: "SoundCloud", color: "#351528" },
        // Yandex
        { id: "alb-ya-1", title: "WILD EA$T", artist: "SALUKI", source: "Yandex", color: "#3A2016" },
        { id: "alb-ya-2", title: "Чарт Яндекс Музыки", artist: "Яндекс Музыка", source: "Yandex", color: "#3D120B" },
        { id: "alb-ya-3", title: "Громкие новинки", artist: "Яндекс Музыка", source: "Yandex", color: "#2C1E38" },
        // YouTube
        { id: "alb-yt-1", title: "YAMAKASI", artist: "Miyagi & Эндшпиль", source: "YouTube", color: "#1A2536" },
        { id: "alb-yt-2", title: "Synthwave Chillout", artist: "Lofi Girl", source: "YouTube", color: "#1F2B1F" },
        { id: "alb-yt-3", title: "YouTube Music Top 100", artist: "YouTube Music", source: "YouTube", color: "#381010" }
    ]

    function getFilteredAlbums() {
        if (!window.activeSourceFilter || window.activeSourceFilter === "All") {
            return window.recommendedAlbums;
        }
        var filter = window.activeSourceFilter.toUpperCase();
        var res = [];
        for (var i = 0; i < window.recommendedAlbums.length; ++i) {
            var item = window.recommendedAlbums[i];
            if (item.source.toUpperCase() === filter) {
                res.push(item);
            }
        }
        return res.length > 0 ? res : window.recommendedAlbums;
    }

    property var allRecommendedTracks: [
        // VK
        { id: "vk-1", title: "Minor", artist: "Miyagi & Andy Panda", durationStr: "2:54", source: "VK", isLibrary: false },
        { id: "vk-2", title: "Самый пьяный округ в районе", artist: "MACAN", durationStr: "2:38", source: "VK", isLibrary: false },
        { id: "vk-3", title: "Сансара", artist: "Баста", durationStr: "3:48", source: "VK", isLibrary: true },
        { id: "vk-4", title: "Костер", artist: "Scirena, Hensy", durationStr: "2:22", source: "VK", isLibrary: false },
        { id: "vk-5", title: "Отпускай", artist: "Три дня дождя", durationStr: "3:05", source: "VK", isLibrary: false },
        // SoundCloud
        { id: "sc-1", title: "Дико, например", artist: "PHARAOH", durationStr: "2:40", source: "SoundCloud", isLibrary: true },
        { id: "sc-2", title: "ТОПСКИЙ ПАВЕЛ", artist: "Boulevard Depo", durationStr: "2:24", source: "SoundCloud", isLibrary: false },
        { id: "sc-3", title: "Close Eyes", artist: "DVRST", durationStr: "2:12", source: "SoundCloud", isLibrary: false },
        { id: "sc-4", title: "ODIUM", artist: "LXST CXNTURY", durationStr: "2:36", source: "SoundCloud", isLibrary: false },
        { id: "sc-5", title: "Why Not", artist: "Ghostface Playa", durationStr: "2:48", source: "SoundCloud", isLibrary: false },
        // Yandex
        { id: "ya-1", title: "Огней", artist: "SALUKI", durationStr: "3:18", source: "Yandex", isLibrary: false },
        { id: "ya-2", title: "По барам", artist: "Anna Asti", durationStr: "3:56", source: "Yandex", isLibrary: true },
        { id: "ya-3", title: "Птички", artist: "HammAli & Navai", durationStr: "2:42", source: "Yandex", isLibrary: false },
        { id: "ya-4", title: "Комета", artist: "JONY", durationStr: "2:38", source: "Yandex", isLibrary: false },
        { id: "ya-5", title: "Credo", artist: "Zivert", durationStr: "3:24", source: "Yandex", isLibrary: false },
        // YouTube
        { id: "yt-1", title: "Beats to relax/study to", artist: "Lofi Girl", durationStr: "3:00", source: "YouTube", isLibrary: true },
        { id: "yt-2", title: "Believer", artist: "Imagine Dragons", durationStr: "3:24", source: "YouTube", isLibrary: false },
        { id: "yt-3", title: "Blinding Lights", artist: "The Weeknd", durationStr: "3:20", source: "YouTube", isLibrary: false },
        { id: "yt-4", title: "In the End", artist: "Linkin Park", durationStr: "3:36", source: "YouTube", isLibrary: true },
        { id: "yt-5", title: "Do I Wanna Know?", artist: "Arctic Monkeys", durationStr: "4:32", source: "YouTube", isLibrary: false }
    ]

    function getFilteredTracks() {
        if (!window.activeSourceFilter || window.activeSourceFilter === "All") {
            return window.allRecommendedTracks;
        }
        var filter = window.activeSourceFilter.toUpperCase();
        var res = [];
        for (var i = 0; i < window.allRecommendedTracks.length; ++i) {
            var track = window.allRecommendedTracks[i];
            if (track.source.toUpperCase() === filter) {
                res.push(track);
            }
        }
        return res.length > 0 ? res : window.allRecommendedTracks;
    }

    function showToast(msg) {
        toastItem.show(msg)
    }

    readonly property color colBgBase: "#09090C"
    readonly property color colBgSurface: "#111116"
    readonly property color colBgSurfaceElevated: "#16161D"
    readonly property color colBgHighlight: "#1F1F28"
    readonly property color colBorderShell: "#14FFFFFF"
    readonly property color colBorderInner: "#0DFFFFFF"
    readonly property color colAccentGreen: "#47B881" // rgb(71, 184, 129)
    readonly property color colAccentGreenDim: "#1F47B881"
    readonly property color colAccentGreenHl: "#1447B881"
    readonly property color colTextPrimary: "#FFFFFF"
    readonly property color colTextSecondary: "#9E9EA8"
    readonly property color colTextSubdued: "#5C5C66"

    // -------------------------------------------------------------------------
    // ОСНОВНОЙ КОНТЕЙНЕР
    // -------------------------------------------------------------------------
    Item {
        anchors.fill: parent

        // ---------------------------------------------------------------------
        // ВЕРХНЯЯ ШАПКА (Динамическая для активной вкладки)
        // ---------------------------------------------------------------------
        Rectangle {
            id: headerBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 56
            color: window.colBgBase
            z: 10

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16
                spacing: 8

                // Заголовок вкладки
                Text {
                    id: headerTitleText
                    text: {
                        switch (window.currentTab) {
                            case 0: return "Избранное"
                            case 1: return "Поиск"
                            case 2: return "Распознавание"
                            case 3: return "Медиатека"
                            case 4: return "Настройки"
                            default: return "VKAudioPlayer"
                        }
                    }
                    font.pixelSize: 22
                    font.bold: true
                    color: window.colTextPrimary
                    Layout.fillWidth: true
                }

                // Правые кнопки в шапке (для вкладки Медиатека: Ссылка + Создать плейлист)
                RowLayout {
                    visible: window.currentTab === 3
                    spacing: 8

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        ArgonIcon {
                            anchors.centerIn: parent
                            name: "link"
                            size: 16
                            color: window.colTextPrimary
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: networkPlaylistModal.open()
                        }
                    }

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        ArgonIcon {
                            anchors.centerIn: parent
                            name: "plus"
                            size: 16
                            color: window.colTextPrimary
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: createPlaylistDialog.open()
                        }
                    }
                }

                // Кнопка выбора источника (ТОЛЬКО ИКОНКА СЕРВИСА + микро-стрелочка)
                // Отображается на вкладках Главная и Поиск
                Rectangle {
                    id: sourceFilterBtn
                    visible: window.currentTab === 0 || window.currentTab === 1
                    width: 44
                    height: 32
                    radius: 16
                    color: window.colBgSurfaceElevated
                    border.color: window.colBorderShell

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        // Иконка активного источника
                        Item {
                            width: 16
                            height: 16

                            // Если Все источники -> Globe
                            ArgonIcon {
                                anchors.centerIn: parent
                                name: "globe"
                                size: 14
                                color: window.colTextSecondary
                                visible: (window.currentTab === 0 ? window.activeSourceFilter : player.searchSource) === "All"
                            }

                            // Если конкретный сервис -> SourceBadge
                            SourceBadge {
                                anchors.centerIn: parent
                                sourceName: window.currentTab === 0 ? window.activeSourceFilter : player.searchSource
                                badgeSize: 11
                                visible: (window.currentTab === 0 ? window.activeSourceFilter : player.searchSource) !== "All"
                            }
                        }

                        ArgonIcon {
                            name: "chevron-down"
                            size: 10
                            color: window.colTextSubdued
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: sourceDropdownMenu.open()
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: window.colBorderInner
            }
        }

        // ---------------------------------------------------------------------
        // ОБЛАСТЬ КОНТЕНТА (5 ЭКРАНОВ)
        // ---------------------------------------------------------------------
        Item {
            anchors.top: headerBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: miniPlayerBar.top
            anchors.bottomMargin: 8
            clip: true

            // -----------------------------------------------------------------
            // ДОЧЕРНИЙ ЭКРАН (Подробный просмотр сервиса, альбома, плейлиста)
            // -----------------------------------------------------------------
            SubScreen {
                id: subScreen
                z: 50
                albumsList: window.recommendedAlbums

                onBackClicked: {
                    subScreen.isOpen = false
                }
                onShuffleClicked: {
                    player.shuffleTracks()
                    window.showToast("Список перемешан и запущен")
                }
                onSortClicked: {
                    sortSheet.open()
                }
                onTrackClicked: function(index) {
                    player.playTrackAtIndex(index)
                }
                onLikeClicked: function(index, isLibrary) {
                    if (isLibrary) {
                        player.removeTrackFromFavorites(index)
                    } else {
                        player.addTrackToFavorites(index)
                    }
                }
                onMoreClicked: function(index, title, artist, isLibrary) {
                    trackActionSheet.trackIndex = index
                    trackActionSheet.trackTitle = title
                    trackActionSheet.trackArtist = artist
                    trackActionSheet.isLibrary = isLibrary
                    trackActionSheet.open()
                }
                onAlbumSelected: function(albumTitle, albumArtist) {
                    player.search(albumTitle)
                    subScreen.title = albumTitle
                    subScreen.subtitle = albumArtist + " • Альбом"
                    subScreen.viewMode = "tracks"
                    subScreen.isOpen = true
                }
            }

            // =================================================================
            // ВКЛАДКА 0: ГЛАВНАЯ (Избранное по сервисам + Рекомендации)
            // =================================================================
            Flickable {
                id: homeFlickable
                visible: window.currentTab === 0
                anchors.fill: parent
                contentWidth: width
                contentHeight: homeColumn.implicitHeight + 30
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ScrollBar.vertical: ScrollBar { active: true }

                ColumnLayout {
                    id: homeColumn
                    width: parent.width - 32
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 16

                    Item { height: 4 } // Отступ сверху

                    // 1. КАРУСЕЛЬ ИЗБРАННОГО ПО СЕРВИСАМ (VK, SC, Yandex, YouTube, Скачанные)
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        contentWidth: favCardsRow.implicitWidth + 8
                        contentHeight: height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: false

                        RowLayout {
                            id: favCardsRow
                            spacing: 12
                            height: parent.height

                            Repeater {
                                model: [
                                    { id: "All", name: "Все", color: "#47B881" },
                                    { id: "VK", name: "ВКонтакте", color: "#0077FF" },
                                    { id: "SoundCloud", name: "SoundCloud", color: "#FF5500" },
                                    { id: "Yandex", name: "Яндекс Музыка", color: "#FC3F1D" },
                                    { id: "YouTube", name: "YouTube Music", color: "#FF0000" },
                                    { id: "Offline", name: "Скачанные", color: "#47B881" }
                                ]

                                delegate: Rectangle {
                                    width: 140
                                    height: 84
                                    radius: 12
                                    color: window.colBgSurfaceElevated
                                    border.color: player.currentSource.toUpperCase() === modelData.id.toUpperCase() ? window.colAccentGreen : window.colBorderShell
                                    border.width: player.currentSource.toUpperCase() === modelData.id.toUpperCase() ? 1.5 : 1

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 4

                                        RowLayout {
                                            Layout.fillWidth: true

                                            SourceBadge {
                                                sourceName: modelData.id
                                                badgeSize: 13
                                            }

                                            Item { Layout.fillWidth: true }

                                            Rectangle {
                                                width: 22
                                                height: 22
                                                radius: 11
                                                color: window.colBgHighlight

                                                ArgonIcon {
                                                    anchors.centerIn: parent
                                                    name: "play"
                                                    size: 10
                                                    color: window.colAccentGreen
                                                }
                                            }
                                        }

                                        Item { Layout.fillHeight: true }

                                        Text {
                                            text: modelData.name
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: window.colTextPrimary
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            player.openFavoritesForSource(modelData.id)
                                            subScreen.title = modelData.name
                                            subScreen.subtitle = (modelData.id === "Offline" ? "Скачанные треки" : (modelData.id === "All" ? "Все источники" : "Избранное " + modelData.name))
                                            subScreen.viewMode = "tracks"
                                            subScreen.isOpen = true
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 2. ЗАГОЛОВОК "РЕКОМЕНДАЦИИ" В СТИЛЕ "ИЗБРАННОЕ"
                    Text {
                        text: "Рекомендации"
                        font.pixelSize: 22
                        font.bold: true
                        color: window.colTextPrimary
                        Layout.topMargin: 8
                    }

                    // 3. СЕКЦИЯ АЛЬБОМОВ
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "Альбомы"
                            font.pixelSize: 16
                            font.bold: true
                            color: window.colTextPrimary
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "Перейти →"
                            font.pixelSize: 12
                            font.bold: true
                            color: window.colAccentGreen

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    subScreen.title = "Все альбомы"
                                    subScreen.subtitle = "Релизы (" + (window.activeSourceFilter === "All" ? "Все источники" : window.activeSourceFilter) + ")"
                                    subScreen.albumsList = window.getFilteredAlbums()
                                    subScreen.viewMode = "albums"
                                    subScreen.isOpen = true
                                }
                            }
                        }
                    }

                    // Виниловая карусель альбомов
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 145
                        contentWidth: albumsRow.implicitWidth + 8
                        contentHeight: height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: false

                        RowLayout {
                            id: albumsRow
                            spacing: 14
                            height: parent.height

                            Repeater {
                                model: window.getFilteredAlbums()

                                delegate: ColumnLayout {
                                    width: 120
                                    spacing: 4

                                    Item {
                                        width: 120
                                        height: 104

                                        // Vinyl Disc slipping behind the jacket
                                        Rectangle {
                                            x: 18
                                            y: 4
                                            width: 96
                                            height: 96
                                            radius: 48
                                            color: "#121216"
                                            border.color: "#1FFFFFFF"

                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 72
                                                height: 72
                                                radius: 36
                                                color: "transparent"
                                                border.color: "#0DFFFFFF"
                                            }
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 48
                                                height: 48
                                                radius: 24
                                                color: "transparent"
                                                border.color: "#12FFFFFF"
                                            }
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 24
                                                height: 24
                                                radius: 12
                                                color: modelData.source === "VK" ? "#0077FF" : (modelData.source === "SoundCloud" ? "#FF5500" : (modelData.source === "Yandex" ? "#FC3F1D" : "#FF0000"))
                                                border.color: "#000000"
                                                border.width: 2
                                            }
                                        }

                                        // Vinyl Jacket
                                        Rectangle {
                                            x: 0
                                            y: 0
                                            width: 100
                                            height: 100
                                            radius: 10
                                            color: modelData.color
                                            border.color: window.colBorderShell

                                            Item {
                                                anchors.centerIn: parent
                                                width: 44
                                                height: 44
                                                ArgonIcon {
                                                    anchors.centerIn: parent
                                                    name: "disc"
                                                    size: 40
                                                    color: "#3A3A48"
                                                }
                                            }

                                            SourceBadge {
                                                anchors.bottom: parent.bottom
                                                anchors.right: parent.right
                                                anchors.margins: 4
                                                sourceName: modelData.source
                                                badgeSize: 11
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: {
                                                    player.search(modelData.title)
                                                    subScreen.title = modelData.title
                                                    subScreen.subtitle = modelData.artist + " • Альбом (" + modelData.source + ")"
                                                    subScreen.viewMode = "tracks"
                                                    subScreen.isOpen = true
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        text: modelData.title
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: window.colTextPrimary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.artist
                                        font.pixelSize: 10
                                        color: window.colTextSecondary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }

                    // 4. СЕКЦИЯ ТРЕКОВ
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4

                        Text {
                            text: "Треки"
                            font.pixelSize: 16
                            font.bold: true
                            color: window.colTextPrimary
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "Перейти →"
                            font.pixelSize: 12
                            font.bold: true
                            color: window.colAccentGreen

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    subScreen.title = "Все треки"
                                    subScreen.subtitle = "Рекомендации (" + (window.activeSourceFilter === "All" ? "Все источники" : window.activeSourceFilter) + ")"
                                    subScreen.viewMode = "tracks"
                                    subScreen.isOpen = true
                                }
                            }
                        }
                    }

                    // Список треков
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Repeater {
                            model: window.getFilteredTracks()

                            delegate: TrackDelegate {
                                Layout.fillWidth: true

                                onTrackClicked: {
                                    player.search(modelData.title + " " + modelData.artist)
                                }
                                onLikeClicked: {
                                    modelData.isLibrary = !modelData.isLibrary
                                    window.showToast(modelData.isLibrary ? "Добавлено в избранное " + modelData.source : "Удалено из избранного " + modelData.source)
                                }
                                onMoreClicked: {
                                    trackActionSheet.trackIndex = -1
                                    trackActionSheet.trackTitle = modelData.title || "Трек"
                                    trackActionSheet.trackArtist = modelData.artist || ""
                                    trackActionSheet.isLibrary = modelData.isLibrary
                                    trackActionSheet.open()
                                }
                            }
                        }
                    }
                }
            }

            // =================================================================
            // ВКЛАДКА 1: ПОИСК (Search)
            // =================================================================
            ColumnLayout {
                id: searchTabContainer
                visible: window.currentTab === 1
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                // Поле ввода поиска
                Rectangle {
                    Layout.fillWidth: true
                    height: 44
                    radius: 10
                    color: window.colBgSurfaceElevated
                    border.color: searchTextInput.activeFocus ? window.colAccentGreen : window.colBorderShell

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 8

                        ArgonIcon {
                            name: "search"
                            size: 16
                            color: window.colTextSubdued
                        }

                        TextField {
                            id: searchTextInput
                            Layout.fillWidth: true
                            placeholderText: "Треки, артисты, альбомы..."
                            placeholderTextColor: window.colTextSubdued
                            color: window.colTextPrimary
                            font.pixelSize: 14
                            background: Item {}
                            onAccepted: {
                                searchDebounce.stop()
                                focus = false
                                Qt.inputMethod.hide()
                                player.search(text)
                            }
                            onTextEdited: {
                                if (text.trim().length >= 2) {
                                    searchDebounce.restart()
                                }
                            }

                            Timer {
                                id: searchDebounce
                                interval: 450
                                onTriggered: {
                                    player.search(searchTextInput.text.trim())
                                }
                            }
                        }

                        Rectangle {
                            width: 24
                            height: 24
                            radius: 12
                            color: "transparent"
                            visible: searchTextInput.text !== ""

                            ArgonIcon {
                                anchors.centerIn: parent
                                name: "close"
                                size: 12
                                color: window.colTextSecondary
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: searchTextInput.text = ""
                            }
                        }
                    }
                }

                // Заголовок результатов поиска
                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: searchTextInput.text !== "" ? ("Результаты для: " + searchTextInput.text) : "Поиск по каталогам"
                        font.pixelSize: 14
                        font.bold: true
                        color: window.colTextSecondary
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "Сервис: " + player.searchSource
                        font.pixelSize: 11
                        color: window.colAccentGreen
                    }
                }

                // Список результатов
                ListView {
                    id: searchListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: trackModel
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBar.vertical: ScrollBar { active: true }

                    delegate: TrackDelegate {
                        width: searchListView.width

                        onTrackClicked: {
                            player.playTrackAtIndex(model.index)
                        }
                        onLikeClicked: {
                            if (model.isLibrary) {
                                player.removeTrackFromFavorites(model.index)
                            } else {
                                player.addTrackToFavorites(model.index)
                            }
                        }
                        onMoreClicked: {
                            trackActionSheet.trackIndex = model.index
                            trackActionSheet.trackTitle = model.title || "Трек"
                            trackActionSheet.trackArtist = model.artist || ""
                            trackActionSheet.isLibrary = model.isLibrary
                            trackActionSheet.open()
                        }
                    }
                }
            }

            // =================================================================
            // ВКЛАДКА 2: РАСПОЗНАВАНИЕ (Shazam / Radar)
            // =================================================================
            ColumnLayout {
                id: shazamTabContainer
                visible: window.currentTab === 2
                anchors.fill: parent
                anchors.margins: 20
                spacing: 24

                Item { Layout.fillHeight: true }

                // Анимированный радар с пульсирующими кольцами
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 220
                    height: 220

                    // Пульсирующие внешние кольца при распознавании
                    Rectangle {
                        id: pulseRing1
                        anchors.centerIn: parent
                        width: 180
                        height: 180
                        radius: 90
                        color: "transparent"
                        border.color: window.colAccentGreen
                        border.width: 1.5
                        opacity: player.isShazamRecognizing ? 0.6 : 0.15

                        NumberAnimation on scale {
                            running: player.isShazamRecognizing
                            loops: Animation.Infinite
                            from: 0.8
                            to: 1.3
                            duration: 1500
                        }
                    }

                    Rectangle {
                        id: pulseRing2
                        anchors.centerIn: parent
                        width: 220
                        height: 220
                        radius: 110
                        color: "transparent"
                        border.color: window.colAccentGreen
                        border.width: 1
                        opacity: player.isShazamRecognizing ? 0.3 : 0.08

                        NumberAnimation on scale {
                            running: player.isShazamRecognizing
                            loops: Animation.Infinite
                            from: 0.9
                            to: 1.45
                            duration: 1500
                        }
                    }

                    // Центральная кнопка радара
                    Rectangle {
                        id: radarCenterBtn
                        anchors.centerIn: parent
                        width: 100
                        height: 100
                        radius: 50
                        color: player.isShazamRecognizing ? window.colAccentGreen : window.colBgSurfaceElevated
                        border.color: window.colAccentGreen
                        border.width: 2

                        ArgonIcon {
                            anchors.centerIn: parent
                            name: "radar"
                            size: 44
                            color: player.isShazamRecognizing ? "#09090C" : window.colAccentGreen
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (player.isShazamRecognizing) {
                                    player.resetShazam()
                                } else {
                                    player.startShazamRecognition()
                                }
                            }
                        }
                    }
                }

                // Статус распознавания
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6

                    Text {
                        text: player.isShazamRecognizing ? "Слушаю аудиопоток..." : (player.shazamResultTitle !== "" ? "Трек распознан!" : "Нажми для поиска трека")
                        font.pixelSize: 18
                        font.bold: true
                        color: window.colTextPrimary
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: player.shazamStatus !== "" ? player.shazamStatus : "Захват системного аудио или микрофона"
                        font.pixelSize: 12
                        color: window.colTextSecondary
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 260
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // Карточка результата распознавания
                Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    radius: 12
                    color: window.colBgSurfaceElevated
                    border.color: window.colAccentGreen
                    visible: player.shazamResultTitle !== ""

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            width: 54
                            height: 54
                            radius: 8
                            color: window.colBgHighlight

                            ArgonIcon {
                                anchors.centerIn: parent
                                name: "disc"
                                size: 28
                                color: window.colAccentGreen
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: player.shazamResultTitle
                                font.pixelSize: 14
                                font.bold: true
                                color: window.colTextPrimary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: player.shazamResultArtist
                                font.pixelSize: 12
                                color: window.colTextSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Rectangle {
                            width: 38
                            height: 38
                            radius: 19
                            color: window.colAccentGreen

                            ArgonIcon {
                                anchors.centerIn: parent
                                name: "play"
                                size: 14
                                color: "#09090C"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    player.search(player.shazamResultTitle + " " + player.shazamResultArtist)
                                    window.currentTab = 1
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // =================================================================
            // ВКЛАДКА 3: МЕДИАТЕКА (Library - Пользовательские и Сохраненные)
            // =================================================================
            ColumnLayout {
                id: libraryTabContainer
                visible: window.currentTab === 3
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 14

                // Фильтр чипов: Все плейлисты, Пользовательские, Сохраненные
                // (Раздел «Скачанные» убран в Избранное на Главной)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: [
                            { id: "all", name: "Все плейлисты" },
                            { id: "custom", name: "Пользовательские" },
                            { id: "saved", name: "Сохраненные" }
                        ]

                        delegate: Rectangle {
                            height: 30
                            width: chipText.implicitWidth + 24
                            radius: 15
                            color: window.libraryFilter === modelData.id ? window.colAccentGreen : window.colBgSurfaceElevated
                            border.color: window.libraryFilter === modelData.id ? window.colAccentGreen : window.colBorderShell

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData.name
                                font.pixelSize: 12
                                font.bold: window.libraryFilter === modelData.id
                                color: window.libraryFilter === modelData.id ? "#09090C" : window.colTextSecondary
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: window.libraryFilter = modelData.id
                            }
                        }
                    }
                }

                // Список плейлистов
                ListView {
                    id: playlistListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    boundsBehavior: Flickable.StopAtBounds

                    model: player.customPlaylists

                    delegate: Rectangle {
                        width: playlistListView.width
                        height: 64
                        radius: 12
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Rectangle {
                                width: 40
                                height: 40
                                radius: 8
                                color: window.colBgHighlight

                                ArgonIcon {
                                    anchors.centerIn: parent
                                    name: "queue"
                                    size: 18
                                    color: window.colAccentGreen
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelData
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: window.colTextPrimary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "Пользовательский плейлист"
                                    font.pixelSize: 11
                                    color: window.colTextSecondary
                                }
                            }

                            // Кнопка удаления
                            Rectangle {
                                width: 32
                                height: 32
                                radius: 16
                                color: "transparent"

                                ArgonIcon {
                                    anchors.centerIn: parent
                                    name: "trash"
                                    size: 14
                                    color: window.colTextSubdued
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: player.deletePlaylist(modelData)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: 48
                            onClicked: {
                                player.playCustomPlaylist(modelData)
                                subScreen.title = modelData
                                subScreen.subtitle = "Пользовательский плейлист"
                                subScreen.viewMode = "tracks"
                                subScreen.isOpen = true
                            }
                        }
                    }
                }

                // Если плейлистов нет
                Rectangle {
                    Layout.fillWidth: true
                    height: 120
                    radius: 12
                    color: window.colBgSurfaceElevated
                    border.color: window.colBorderShell
                    visible: player.customPlaylists.length === 0

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            text: "Нет созданных плейлистов"
                            font.pixelSize: 13
                            color: window.colTextSecondary
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 140
                            height: 32
                            radius: 8
                            color: window.colAccentGreen

                            Text {
                                anchors.centerIn: parent
                                text: "Создать плейлист"
                                font.pixelSize: 12
                                font.bold: true
                                color: "#09090C"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: createPlaylistDialog.open()
                            }
                        }
                    }
                }
            }

            // =================================================================
            // ВКЛАДКА 4: НАСТРОЙКИ (Settings)
            // =================================================================
            Flickable {
                id: settingsFlickable
                visible: window.currentTab === 4
                anchors.fill: parent
                contentWidth: width
                contentHeight: settingsCol.implicitHeight + 40
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ScrollBar.vertical: ScrollBar { active: true }

                ColumnLayout {
                    id: settingsCol
                    width: parent.width - 32
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 16

                    Item { height: 2 }

                    // Секция 1: Подключенные сервисы
                    Text {
                        text: "Подключенные сервисы"
                        font.pixelSize: 14
                        font.bold: true
                        color: window.colTextSecondary
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: servicesListCol.implicitHeight + 16
                        radius: 12
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        ColumnLayout {
                            id: servicesListCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            // VK
                            RowLayout {
                                Layout.fillWidth: true

                                SourceBadge { sourceName: "VK"; badgeSize: 14 }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: "VKontakte"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: player.isAuthorized ? "Авторизован • Синхронизировано" : "Не авторизован"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Text {
                                    text: player.isAuthorized ? "ВЫЙТИ" : "ВОЙТИ"
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: player.isAuthorized ? "#E05D5D" : window.colAccentGreen

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (player.isAuthorized) {
                                                player.logout("VK")
                                            } else {
                                                authDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: window.colBorderInner }

                            // SoundCloud
                            RowLayout {
                                Layout.fillWidth: true

                                SourceBadge { sourceName: "SoundCloud"; badgeSize: 14 }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: "SoundCloud"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: "Публичный API / OAuth"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Text {
                                    text: "ВХОД WEB"
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: window.colAccentGreen

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: player.startWebViewAuth("SoundCloud")
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: window.colBorderInner }

                            // Yandex Music
                            RowLayout {
                                Layout.fillWidth: true

                                SourceBadge { sourceName: "Yandex"; badgeSize: 14 }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: "Яндекс Музыка"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: "Токен авторизации"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Text {
                                    text: "НАСТРОИТЬ"
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: window.colAccentGreen

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: player.startWebViewAuth("Yandex")
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: window.colBorderInner }

                            // YouTube Music
                            RowLayout {
                                Layout.fillWidth: true

                                SourceBadge { sourceName: "YouTube"; badgeSize: 14 }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: "YouTube Music"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: "Сессия и po_token"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Text {
                                    text: "НАСТРОИТЬ"
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: window.colAccentGreen

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: player.startWebViewAuth("YouTube")
                                    }
                                }
                            }
                        }
                    }

                    // Секция 2: Воспроизведение и Аудио
                    Text {
                        text: "Воспроизведение и Аудио"
                        font.pixelSize: 14
                        font.bold: true
                        color: window.colTextSecondary
                        Layout.topMargin: 4
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: audioSettingsCol.implicitHeight + 16
                        radius: 12
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        ColumnLayout {
                            id: audioSettingsCol
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Item {
                                Layout.fillWidth: true
                                implicitHeight: eqRowLayout.implicitHeight

                                RowLayout {
                                    id: eqRowLayout
                                    anchors.fill: parent

                                    ArgonIcon { name: "sliders"; size: 16; color: window.colAccentGreen }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: "10-полосный эквалайзер"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                        Text { text: "DSP Biquad фильтры 32-bit Float"; font.pixelSize: 11; color: window.colTextSecondary }
                                    }

                                    Text {
                                        text: "Настроить →"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: window.colAccentGreen
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: equalizerSheet.open()
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: window.colBorderInner }

                            // Кроссфейд
                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "Кроссфейд (плавный переход)"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: player.isCrossfade ? (player.crossfadeDuration + " сек") : "Выключен"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Switch {
                                    checked: player.isCrossfade
                                    onToggled: player.setCrossfade(checked)
                                }
                            }

                            // Слайдер кроссфейда
                            RowLayout {
                                Layout.fillWidth: true
                                visible: player.isCrossfade
                                spacing: 8

                                Text { text: "1с"; font.pixelSize: 10; color: window.colTextSubdued }

                                Slider {
                                    Layout.fillWidth: true
                                    from: 1
                                    to: 12
                                    stepSize: 1
                                    value: player.crossfadeDuration
                                    onMoved: player.setCrossfadeDuration(Math.round(value))
                                }

                                Text { text: "12с"; font.pixelSize: 10; color: window.colTextSubdued }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: window.colBorderInner }

                            // Кэширование аудио
                            RowLayout {
                                Layout.fillWidth: true

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "Оффлайн кэш треков"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary }
                                    Text { text: player.isAudioCacheEnabled ? (player.audioCacheUsedMb + " МБ / " + player.audioCacheMaxMb + " МБ") : "Выключено"; font.pixelSize: 11; color: window.colTextSecondary }
                                }

                                Switch {
                                    checked: player.isAudioCacheEnabled
                                    onToggled: player.setAudioCacheEnabled(checked)
                                }
                            }

                            // Очистка кэша
                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                radius: 8
                                color: window.colBgHighlight
                                visible: player.audioCacheUsedMb > 0

                                Text {
                                    anchors.centerIn: parent
                                    text: "Очистить кэш (" + player.audioCacheUsedMb + " МБ)"
                                    font.pixelSize: 11
                                    font.bold: true
                                    color: window.colTextPrimary
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: player.clearAudioCache()
                                }
                            }
                        }
                    }

                    // Секция 3: Система и Диагностика
                    Text {
                        text: "Система и Диагностика"
                        font.pixelSize: 14
                        font.bold: true
                        color: window.colTextSecondary
                        Layout.topMargin: 4
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 48
                        radius: 12
                        color: window.colBgSurfaceElevated
                        border.color: window.colBorderShell

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            ArgonIcon { name: "sliders"; size: 16; color: window.colTextSecondary }
                            Text { text: "Журнал логов приложения"; font.pixelSize: 13; font.bold: true; color: window.colTextPrimary; Layout.fillWidth: true }
                            Text { text: "Открыть →"; font.pixelSize: 12; color: window.colAccentGreen; font.bold: true }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: logsDialog.open()
                        }
                    }

                    Text {
                        text: "VKAudioPlayer v2.4 Native • C++20 / miniaudio / Qt6"
                        font.pixelSize: 11
                        font.family: "JetBrains Mono"
                        color: window.colTextSubdued
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 12
                        Layout.bottomMargin: 16
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // МИНИ-ПЛЕЕР (Плавающая панель над нижней навигацией)
        // ---------------------------------------------------------------------
        Rectangle {
            id: miniPlayerBar
            anchors.bottom: bottomNavBar.top
            anchors.bottomMargin: 8
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            height: 58
            radius: 12
            color: window.colBgSurfaceElevated
            border.color: window.colBorderShell
            z: 20

            // Верхний тонкий прогресс-бар трека
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 2
                radius: 1
                color: "#1AFFFFFF"

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    width: parent.width * (player.durationMs > 0 ? (player.positionMs / player.durationMs) : 0)
                    color: window.colAccentGreen
                    radius: 1
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 10
                spacing: 10

                // Обложка в мини-плеере с бейджем источника
                Rectangle {
                    width: 42
                    height: 42
                    radius: 8
                    color: window.colBgHighlight
                    border.color: window.colBorderInner
                    clip: true

                    Image {
                        id: miniCoverImg
                        anchors.fill: parent
                        source: player.currentCoverUrl
                        sourceSize: Qt.size(100, 100)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: player.currentCoverUrl !== "" && status === Image.Ready
                    }

                    Item {
                        anchors.centerIn: parent
                        visible: !miniCoverImg.visible
                        width: 20
                        height: 20

                        ArgonIcon {
                            anchors.centerIn: parent
                            name: "disc"
                            size: 18
                            color: "#3A3A48"
                        }
                    }

                    SourceBadge {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.margins: 2
                        sourceName: player.currentTrackSource
                        badgeSize: 10
                    }
                }

                // Название и артист
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Text {
                        text: player.currentTitle !== "" ? player.currentTitle : "Не играет"
                        font.pixelSize: 13
                        font.bold: true
                        color: window.colTextPrimary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Text {
                        text: player.currentArtist !== "" ? player.currentArtist : "Нажмите для открытия плеера"
                        font.pixelSize: 11
                        color: window.colTextSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Кнопка Лайк
                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: "transparent"

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: player.isCurrentFavorite ? "heart-fill" : "heart"
                        size: 16
                        color: player.isCurrentFavorite ? window.colAccentGreen : window.colTextSubdued
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.toggleCurrentFavorite()
                    }
                }

                // Кнопка Предыдущий трек
                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: "transparent"

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: "prev"
                        size: 14
                        color: window.colTextPrimary
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.prevTrack()
                    }
                }

                // Кнопка Play/Pause
                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: window.colAccentGreen

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: player.isPlaying ? "pause" : "play"
                        size: 16
                        color: "#09090C"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.togglePlay()
                    }
                }

                // Кнопка Следующий трек
                Rectangle {
                    width: 32
                    height: 32
                    radius: 16
                    color: "transparent"

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: "next"
                        size: 14
                        color: window.colTextPrimary
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: player.nextTrack()
                    }
                }
            }

            // Клик по телу мини-плеера открывает полноэкранный плеер
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 165 // Не блокирует кнопки управления мини-плеера
                onClicked: nowPlayingSheet.open()
            }
        }

        // ---------------------------------------------------------------------
        // НИЖНЯЯ ПАНЕЛЬ НАВИГАЦИИ (5 ЧИСТЫХ ИКОНОК БЕЗ ПОДПИСЕЙ И ЭМОДЗИ)
        // ---------------------------------------------------------------------
        Rectangle {
            id: bottomNavBar
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 60
            color: window.colBgSurface
            z: 20

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: window.colBorderInner
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // 1. Главная (Home)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        ArgonIcon {
                            name: "home"
                            size: 20
                            color: window.currentTab === 0 ? window.colAccentGreen : window.colTextSubdued
                        }

                        // Индикатор активной вкладки
                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: window.colAccentGreen
                            visible: window.currentTab === 0
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentTab = 0
                    }
                }

                // 2. Поиск (Search)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        ArgonIcon {
                            name: "search"
                            size: 20
                            color: window.currentTab === 1 ? window.colAccentGreen : window.colTextSubdued
                        }

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: window.colAccentGreen
                            visible: window.currentTab === 1
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentTab = 1
                    }
                }

                // 3. Распознавание (Shazam / Radar)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        ArgonIcon {
                            name: "radar"
                            size: 22
                            color: window.currentTab === 2 ? window.colAccentGreen : window.colTextSubdued
                        }

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: window.colAccentGreen
                            visible: window.currentTab === 2
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentTab = 2
                    }
                }

                // 4. Медиатека (Library)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        ArgonIcon {
                            name: "library"
                            size: 20
                            color: window.currentTab === 3 ? window.colAccentGreen : window.colTextSubdued
                        }

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: window.colAccentGreen
                            visible: window.currentTab === 3
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentTab = 3
                    }
                }

                // 5. Настройки (Settings)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        ArgonIcon {
                            name: "settings"
                            size: 20
                            color: window.currentTab === 4 ? window.colAccentGreen : window.colTextSubdued
                        }

                        Rectangle {
                            width: 4
                            height: 4
                            radius: 2
                            color: window.colAccentGreen
                            visible: window.currentTab === 4
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: window.currentTab = 4
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // ВЫПАДАЮЩЕЕ МЕНЮ ИСТОЧНИКОВ (Source Dropdown Menu)
    // -------------------------------------------------------------------------
    Popup {
        id: sourceDropdownMenu
        x: window.width - width - 16
        y: 52
        width: 190
        padding: 6
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: window.colBgSurfaceElevated
            radius: 12
            border.color: window.colBorderShell
        }

        ColumnLayout {
            width: parent.width
            spacing: 2

            Repeater {
                model: [
                    { id: "All", name: "Все источники" },
                    { id: "VK", name: "VKontakte" },
                    { id: "SoundCloud", name: "SoundCloud" },
                    { id: "Yandex", name: "Яндекс Музыка" },
                    { id: "YouTube", name: "YouTube Music" },
                    { id: "Offline", name: "Скачанные" }
                ]

                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 38
                    radius: 8
                    color: itemMouse.pressed ? window.colBgHighlight : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        SourceBadge {
                            sourceName: modelData.id
                            badgeSize: 16
                        }

                        Text {
                            text: modelData.name
                            font.pixelSize: 12
                            font.bold: (window.currentTab === 0 ? window.activeSourceFilter : player.searchSource) === modelData.id
                            color: (window.currentTab === 0 ? window.activeSourceFilter : player.searchSource) === modelData.id ? window.colAccentGreen : window.colTextPrimary
                            Layout.fillWidth: true
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        onClicked: {
                            if (window.currentTab === 0) {
                                window.activeSourceFilter = modelData.id
                                if (modelData.id !== "All") {
                                    player.switchSource(modelData.id)
                                }
                                window.showToast("Источник: " + modelData.name)
                            } else {
                                player.searchSource = modelData.id
                                window.showToast("Поиск: " + modelData.name)
                            }
                            sourceDropdownMenu.close()
                        }
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // КОНТЕКСТНОЕ МЕНЮ ДЕЙСТВИЙ С ТРЕКОМ (•••)
    // -------------------------------------------------------------------------
    TrackActionSheet {
        id: trackActionSheet

        onPlayRequested: function(index) {
            if (index >= 0) {
                player.playTrackAtIndex(index)
            } else {
                player.play()
            }
        }
        onFavoriteToggled: function(index, isLibrary) {
            if (index >= 0) {
                if (isLibrary) {
                    player.removeTrackFromFavorites(index)
                } else {
                    player.addTrackToFavorites(index)
                }
            } else {
                player.toggleCurrentFavorite()
            }
        }
        onDownloadRequested: function(index) {
            if (index >= 0) {
                player.downloadTrack(index)
            } else {
                player.downloadCurrentTrack()
            }
        }
        onQueueToggled: function(index) {
            if (index >= 0) {
                if (player.isTrackInQueue(index)) {
                    player.removeTrackFromQueue(index)
                    window.showToast("Удалено из очереди")
                } else {
                    player.addTrackToQueue(index)
                    window.showToast("Добавлено в очередь")
                }
            }
        }
        onAddToPlaylistRequested: function(index) {
            window.selectedTrackIndex = index
            addToPlaylistDialog.open()
        }
        onGoToAlbumRequested: function(index, title, artist) {
            player.search(title)
            subScreen.title = title
            subScreen.subtitle = artist + " • Альбом"
            subScreen.viewMode = "tracks"
            subScreen.isOpen = true
        }
    }

    // -------------------------------------------------------------------------
    // ДИАЛОГ: СОЗДАНИЕ НОВОГО ПЛЕЙЛИСТА
    // -------------------------------------------------------------------------
    Dialog {
        id: createPlaylistDialog
        x: parent ? ((parent.width - width) / 2) : 0
        y: parent ? ((parent.height - height) / 2) : 0
        width: parent ? Math.min(parent.width * 0.85, 340) : 320
        height: crPlCol.implicitHeight + 32
        padding: 0
        z: 1000
        modal: true
        focus: true

        background: Rectangle {
            color: window.colBgSurfaceElevated
            radius: 12
            border.color: window.colBorderShell
        }

        ColumnLayout {
            id: crPlCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "Создать плейлист"
                font.pixelSize: 16
                font.bold: true
                color: window.colTextPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                height: 38
                radius: 8
                color: window.colBgHighlight

                TextField {
                    id: newPlInput
                    anchors.fill: parent
                    anchors.margins: 8
                    placeholderText: "Название плейлиста"
                    placeholderTextColor: window.colTextSubdued
                    color: window.colTextPrimary
                    font.pixelSize: 13
                    background: Item {}
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    height: 34
                    radius: 8
                    color: window.colBgHighlight

                    Text {
                        anchors.centerIn: parent
                        text: "Отмена"
                        font.pixelSize: 12
                        color: window.colTextSecondary
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: createPlaylistDialog.close()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 34
                    radius: 8
                    color: window.colAccentGreen

                    Text {
                        anchors.centerIn: parent
                        text: "Создать"
                        font.pixelSize: 12
                        font.bold: true
                        color: "#09090C"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (newPlInput.text.trim() !== "") {
                                player.createPlaylist(newPlInput.text.trim())
                                newPlInput.text = ""
                                createPlaylistDialog.close()
                            }
                        }
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // ДИАЛОГ: ДОБАВИТЬ ТРЕК В ПЛЕЙЛИСТ
    // -------------------------------------------------------------------------
    Dialog {
        id: addToPlaylistDialog
        x: parent ? ((parent.width - width) / 2) : 0
        y: parent ? ((parent.height - height) / 2) : 0
        width: parent ? Math.min(parent.width * 0.85, 340) : 320
        height: addPlCol.implicitHeight + 32
        padding: 0
        z: 1000
        modal: true
        focus: true

        background: Rectangle {
            color: window.colBgSurfaceElevated
            radius: 12
            border.color: window.colBorderShell
        }

        ColumnLayout {
            id: addPlCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "Выберите плейлист"
                font.pixelSize: 16
                font.bold: true
                color: window.colTextPrimary
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(200, count * 42)
                model: player.customPlaylists
                clip: true

                delegate: Rectangle {
                    width: parent.width
                    height: 38
                    radius: 6
                    color: plItemM.pressed ? window.colBgHighlight : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        spacing: 8
                        ArgonIcon { name: "queue"; size: 14; color: window.colAccentGreen }
                        Text { text: modelData; font.pixelSize: 13; color: window.colTextPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                    }

                    MouseArea {
                        id: plItemM
                        anchors.fill: parent
                        onClicked: {
                            player.addTrackToPlaylist(modelData, window.selectedTrackIndex)
                            addToPlaylistDialog.close()
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 34
                radius: 8
                color: window.colBgHighlight

                Text {
                    anchors.centerIn: parent
                    text: "Закрыть"
                    font.pixelSize: 12
                    color: window.colTextSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: addToPlaylistDialog.close()
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // ВСТРОЕННЫЕ ЛИСТЫ И ДИАЛОГИ (NowPlaying, Auth, Logs, WebView, EQ, Sort, NetworkPlaylist, Toast)
    // -------------------------------------------------------------------------
    NowPlayingSheet {
        id: nowPlayingSheet
        onOpenEqualizer: equalizerSheet.open()
        onOpenMoreActions: {
            trackActionSheet.trackIndex = -1
            trackActionSheet.trackTitle = player.currentTitle
            trackActionSheet.trackArtist = player.currentArtist
            trackActionSheet.isLibrary = player.isCurrentFavorite
            trackActionSheet.open()
        }
    }

    SortSheet {
        id: sortSheet
        onSortSelected: function(mode, label) {
            subScreen.sortLabel = label
            player.sortTracks(mode)
            window.showToast("Отсортировано: " + label)
        }
    }

    EqualizerSheet {
        id: equalizerSheet
    }

    NetworkPlaylistModal {
        id: networkPlaylistModal
        onImportRequested: function(url) {
            player.importNetworkPlaylist(url)
            window.showToast("Импорт: " + url)
        }
    }

    Toast {
        id: toastItem
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 140
    }

    AuthDialog {
        id: authDialog
    }

    Loader {
        id: authWebViewLoader
        anchors.fill: parent
        active: player.isWebViewAuthOpen
        source: "AuthWebViewSheet.qml"
        z: 200
    }

    LogsDialog {
        id: logsDialog
    }

    Connections {
        target: player
        function onAuthDialogOpenChanged(open) {
            if (open) authDialog.open()
            else authDialog.close()
        }
        function onStatusMessageChanged(msg) {
            if (msg && msg !== "") {
                if (msg.indexOf("[VK] Проверка") === 0 || 
                    msg.indexOf("[YouTube] Сессия") === 0 || 
                    msg.indexOf("[SoundCloud] Проверка") === 0) {
                    return;
                }
                window.showToast(msg)
            }
        }
    }
}
