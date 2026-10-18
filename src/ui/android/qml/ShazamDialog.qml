import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: shazamPopup
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.90, 420)
    height: Math.min(parent.height * 0.70, 520)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#1A1A1D"
        radius: 16
        border.color: "#2E2E33"
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // =====================================================================
        // Заголовок
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "️"
                font.pixelSize: 22
            }

            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true

                Text {
                    text: "Распознавание музыки"
                    font.pixelSize: 16
                    font.bold: true
                    color: "#FFFFFF"
                }

                Text {
                    text: "Определение играющего трека через Shazam"
                    font.pixelSize: 11
                    color: "#909499"
                }
            }

            // Кнопка закрытия
            Rectangle {
                width: 30
                height: 30
                radius: 15
                color: closeMouse.containsMouse ? "#323238" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 14
                    color: "#909499"
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shazamPopup.close()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#2E2E33"
        }

        // =====================================================================
        // Центральная область анимации и статуса
        // =====================================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 14
                width: parent.width

                // Большая круглая кнопка с пульсацией
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 110
                    height: 110

                    // Пульсирующий ореол
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width + (player.isShazamRecognizing ? 24 : 0)
                        height: parent.height + (player.isShazamRecognizing ? 24 : 0)
                        radius: width / 2
                        color: "#2688EB"
                        opacity: player.isShazamRecognizing ? 0.25 : 0.0

                        Behavior on width {
                            NumberAnimation { duration: 400 }
                        }
                        Behavior on height {
                            NumberAnimation { duration: 400 }
                        }
                    }

                    Rectangle {
                        id: micCircle
                        anchors.fill: parent
                        radius: width / 2
                        color: player.isShazamRecognizing ? "#2688EB" : (micMouse.containsMouse ? "#2A4060" : "#1E2B3E")
                        border.color: "#529EF4"
                        border.width: player.isShazamRecognizing ? 3 : 2

                        Text {
                            anchors.centerIn: parent
                            text: player.isShazamRecognizing ? "⚡" : "️"
                            font.pixelSize: 42
                        }

                        MouseArea {
                            id: micMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (!player.isShazamRecognizing) {
                                    player.startShazamRecognition()
                                }
                            }
                        }
                    }
                }

                // Статус
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: parent.width - 24
                    text: player.shazamStatus
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: player.isShazamRecognizing ? "#529EF4" : "#909499"
                }

                // Карточка распознанного трека
                Rectangle {
                    visible: player.shazamResultTitle !== ""
                    Layout.fillWidth: true
                    Layout.maximumWidth: parent.width - 20
                    Layout.alignment: Qt.AlignHCenter
                    height: recognizedCol.implicitHeight + 20
                    radius: 12
                    color: "#222B38"
                    border.color: "#2688EB"
                    border.width: 1

                    ColumnLayout {
                        id: recognizedCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        Text {
                            text: "  " + player.shazamResultTitle
                            font.pixelSize: 15
                            font.bold: true
                            color: "#FFFFFF"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: "  " + player.shazamResultArtist
                            font.pixelSize: 13
                            color: "#529EF4"
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        // Кнопка перехода к поиску трека в плеере
                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: "#2688EB"
                            Layout.topMargin: 4

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6
                                Text { text: ""; font.pixelSize: 13 }
                                Text {
                                    text: "Найти в поиске плеера"
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: "#FFFFFF"
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    player.setSearchSource("All")
                                    player.search(player.shazamResultArtist + " " + player.shazamResultTitle)
                                    player.activeTabIndex = 1
                                    shazamPopup.close()
                                }
                            }
                        }
                    }
                }
            }
        }

        // =====================================================================
        // Нижняя панель действий
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                height: 42
                radius: 10
                color: player.isShazamRecognizing ? "#3A3A40" : "#2688EB"

                Text {
                    anchors.centerIn: parent
                    text: player.isShazamRecognizing ? "Идет распознавание..." : "️ Начать запись (7 сек)"
                    font.pixelSize: 13
                    font.bold: true
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !player.isShazamRecognizing
                    onClicked: player.startShazamRecognition()
                }
            }

            Rectangle {
                width: 90
                height: 42
                radius: 10
                color: "#2C2D31"
                border.color: "#38393D"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Закрыть"
                    font.pixelSize: 13
                    color: "#E1E3E6"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: shazamPopup.close()
                }
            }
        }
    }
}
