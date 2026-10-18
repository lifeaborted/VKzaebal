import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    y: 0
    x: isOpen ? 0 : (parent ? parent.width : 1000)
    visible: isOpen || (parent && x < parent.width)
    Behavior on x {
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }

    property bool isOpen: false
    property string title: ""
    property string subtitle: ""
    property string sortLabel: "По дате добавления"
    property string viewMode: "tracks" // "tracks" or "albums"
    property var albumsList: []

    signal backClicked()
    signal shuffleClicked()
    signal sortClicked()
    signal trackClicked(int index)
    signal likeClicked(int index, bool isLibrary)
    signal moreClicked(int index, string title, string artist, bool isLibrary)
    signal albumSelected(string albumTitle, string albumArtist)

    readonly property color colBgBase: "#09090C"
    readonly property color colBgSurfaceElevated: "#16161D"
    readonly property color colBgHighlight: "#1F1F28"
    readonly property color colBorderShell: "#14FFFFFF"
    readonly property color colBorderInner: "#0DFFFFFF"
    readonly property color colAccentGreen: "#47B881"
    readonly property color colTextPrimary: "#FFFFFF"
    readonly property color colTextSecondary: "#9E9EA8"
    readonly property color colTextSubdued: "#5C5C66"

    Rectangle {
        anchors.fill: parent
        color: root.colBgBase
        MouseArea { anchors.fill: parent }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Top Bar
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Rectangle {
                width: 36
                height: 36
                radius: 18
                color: root.colBgSurfaceElevated
                border.color: root.colBorderShell

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "arrow-left"
                    size: 16
                    color: root.colTextPrimary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.backClicked()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: root.title
                    font.pixelSize: 20
                    font.bold: true
                    color: root.colTextPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: root.subtitle
                    font.pixelSize: 12
                    color: root.colTextSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        // Action Controls Pill Row (Перемешать + Сортировка)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.viewMode === "tracks"

            // Кнопка: Перемешать (Primary)
            Rectangle {
                height: 34
                width: shuffleRow.implicitWidth + 24
                radius: 17
                color: "#FFFFFF"

                RowLayout {
                    id: shuffleRow
                    anchors.centerIn: parent
                    spacing: 6

                    ArgonIcon {
                        name: "shuffle"
                        size: 14
                        color: "#000000"
                    }

                    Text {
                        text: "Перемешать"
                        font.pixelSize: 12
                        font.bold: true
                        color: "#000000"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.shuffleClicked()
                }
            }

            // Кнопка: Сортировка
            Rectangle {
                height: 34
                width: sortRow.implicitWidth + 24
                radius: 17
                color: root.colBgSurfaceElevated
                border.color: root.colBorderShell

                RowLayout {
                    id: sortRow
                    anchors.centerIn: parent
                    spacing: 6

                    ArgonIcon {
                        name: "sort"
                        size: 14
                        color: root.colTextSecondary
                    }

                    Text {
                        text: root.sortLabel
                        font.pixelSize: 12
                        font.bold: true
                        color: root.colTextPrimary
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.sortClicked()
                }
            }

            Item { Layout.fillWidth: true }
        }

        // Содержимое: Список треков
        ListView {
            id: trackListView
            visible: root.viewMode === "tracks"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: trackModel

            ScrollBar.vertical: ScrollBar { active: true }

            delegate: TrackDelegate {
                width: trackListView.width

                onTrackClicked: root.trackClicked(model.index)
                onLikeClicked: root.likeClicked(model.index, model.isLibrary)
                onMoreClicked: root.moreClicked(model.index, model.title || "Трек", model.artist || "", model.isLibrary)
            }
        }

        // Содержимое: Сетка альбомов (для режима "Все альбомы")
        GridView {
            id: albumsGrid
            visible: root.viewMode === "albums"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: width / 2
            cellHeight: cellWidth + 40
            boundsBehavior: Flickable.StopAtBounds
            model: root.albumsList

            ScrollBar.vertical: ScrollBar { active: true }

            delegate: ColumnLayout {
                width: albumsGrid.cellWidth - 10
                spacing: 6

                Rectangle {
                    width: parent.width
                    height: width
                    radius: 12
                    color: modelData.color || "#1E1E28"
                    border.color: root.colBorderShell
                    clip: true

                    ArgonIcon {
                        anchors.centerIn: parent
                        name: "disc"
                        size: parent.width * 0.45
                        color: "#3A3A48"
                    }

                    SourceBadge {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.margins: 6
                        sourceName: modelData.source
                        badgeSize: 13
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.albumSelected(modelData.title, modelData.artist)
                    }
                }

                Text {
                    text: modelData.title
                    font.pixelSize: 13
                    font.bold: true
                    color: root.colTextPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Text {
                    text: modelData.artist
                    font.pixelSize: 11
                    color: root.colTextSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
}
