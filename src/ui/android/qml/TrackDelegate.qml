import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    width: parent ? parent.width : 360
    height: 60
    radius: 10

    readonly property var itemData: (typeof modelData !== "undefined" && modelData !== null) ? modelData : model

    color: (itemData && itemData.isPlaying) ? "#1447B881" : (itemMouse.pressed ? "#16161D" : "transparent")

    signal trackClicked()
    signal likeClicked()
    signal moreClicked()

    // Клик по телу трека для воспроизведения
    MouseArea {
        id: itemMouse
        anchors.fill: parent
        anchors.rightMargin: 80 // Надежный отступ, чтобы не перекрывать кнопки лайка и меню
        onClicked: root.trackClicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 12
        z: 2

        // Обложка трека с микро-бейджем источника
        Rectangle {
            width: 44
            height: 44
            radius: 6
            color: "#16161D"
            border.color: "#14FFFFFF"
            clip: true

            Image {
                id: trackThumb
                anchors.fill: parent
                source: (root.itemData && root.itemData.thumbnailUrl) || ""
                sourceSize: Qt.size(120, 120)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: source !== "" && status === Image.Ready
            }

            // Плейсхолдер винила если нет обложки
            Item {
                anchors.centerIn: parent
                visible: !trackThumb.visible
                width: 24
                height: 24

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "disc"
                    size: 20
                    color: "#30303A"
                }
            }

            // Микро-бейдж источника в правом нижнем углу обложки
            SourceBadge {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 2
                sourceName: (root.itemData && root.itemData.source) || "VK"
                badgeSize: 11
            }
        }

        // Название трека и артист
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: (root.itemData && root.itemData.title) || "Без названия"
                font.pixelSize: 13
                font.bold: true
                color: (root.itemData && root.itemData.isPlaying) ? "#47B881" : "#FFFFFF"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: (root.itemData && root.itemData.artist) !== "" ? root.itemData.artist : "Неизвестный исполнитель"
                    font.pixelSize: 11
                    color: "#9E9EA8"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Text {
                    text: (root.itemData && root.itemData.durationStr) || ""
                    font.pixelSize: 10
                    font.family: "JetBrains Mono"
                    color: "#5C5C66"
                    visible: text !== ""
                }
            }
        }

        // Две кнопки справа: Лайк и •••
        RowLayout {
            Layout.preferredWidth: 84
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            spacing: 4

            // Кнопка Лайк
            Item {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 44

                ArgonIcon {
                    anchors.centerIn: parent
                    name: (root.itemData && root.itemData.isLibrary) ? "heart-fill" : "heart"
                    size: 17
                    color: (root.itemData && root.itemData.isLibrary) ? "#47B881" : "#5C5C66"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.likeClicked()
                }
            }

            // Кнопка •••
            Item {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 44

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "more"
                    size: 17
                    color: "#9E9EA8"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.moreClicked()
                }
            }
        }
    }
}
