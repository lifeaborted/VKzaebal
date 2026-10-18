import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: logsPopup
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.94, 540)
    height: Math.min(parent.height * 0.90, 680)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#1A1A1D"
        radius: 16
        border.color: "#2E2E33"
        border.width: 1
    }

    property bool autoScroll: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // =====================================================================
        // Заголовок и быстрые действия
        // =====================================================================
        // Заголовок и кнопки управления
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ArgonIcon {
                name: "sliders"
                size: 20
                color: "#47B881"
            }

            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true

                Text {
                    text: "Журнал логов"
                    font.pixelSize: 16
                    font.bold: true
                    color: "#FFFFFF"
                }

                Text {
                    text: "Диагностика и события плеера в реальном времени"
                    font.pixelSize: 11
                    color: "#909499"
                }
            }

            // Кнопка «Скопировать»
            Rectangle {
                width: 34
                height: 34
                radius: 8
                color: copyMouse.containsMouse ? "#323238" : "#242428"
                border.color: "#383840"
                border.width: 1

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "queue"
                    size: 16
                    color: "#FFFFFF"
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: player.copyLogsToClipboard()
                }
            }

            // Кнопка «Очистить»
            Rectangle {
                width: 34
                height: 34
                radius: 8
                color: clearMouse.containsMouse ? "#323238" : "#242428"
                border.color: "#383840"
                border.width: 1

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "trash"
                    size: 16
                    color: "#E05D5D"
                }

                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: player.clearLogs()
                }
            }

            // Кнопка «Закрыть»
            Rectangle {
                width: 34
                height: 34
                radius: 17
                color: closeMouse.containsMouse ? "#36363B" : "#242428"
                border.color: "#383840"
                border.width: 1

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
                    onClicked: logsPopup.close()
                }
            }
        }

        // =====================================================================
        // Терминал вывода логов
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: "#0F0F12"
            border.color: "#26262B"
            border.width: 1
            clip: true

            Flickable {
                id: logFlickable
                anchors.fill: parent
                anchors.margins: 10
                contentWidth: width
                contentHeight: logTextArea.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                TextArea.flickable: TextArea {
                    id: logTextArea
                    readOnly: true
                    selectByMouse: true
                    text: player.fullLogs !== "" ? player.fullLogs : "Ожидание записей в журнал..."
                    color: player.fullLogs !== "" ? "#4ADE80" : "#76787A"
                    font.family: "monospace"
                    font.pixelSize: 11
                    wrapMode: TextEdit.WrapAnywhere
                    background: Item {}
                    padding: 0
                }

                ScrollBar.vertical: ScrollBar {
                    id: vScrollBar
                    policy: ScrollBar.AsNeeded
                }
            }

            Connections {
                target: player
                function onLogsUpdated() {
                    if (logsPopup.autoScroll) {
                        Qt.callLater(function() {
                            logFlickable.contentY = Math.max(0, logFlickable.contentHeight - logFlickable.height)
                        })
                    }
                }
            }
        }

        // =====================================================================
        // Нижняя панель действий и статус
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                height: 28
                radius: 14
                color: logsPopup.autoScroll ? "#243224" : "#252528"
                border.color: logsPopup.autoScroll ? "#4BB34B" : "#38383D"
                border.width: 1
                Layout.preferredWidth: autoScrollRow.implicitWidth + 16

                RowLayout {
                    id: autoScrollRow
                    anchors.centerIn: parent
                    spacing: 6

                    Text {
                        text: logsPopup.autoScroll ? "●" : "○"
                        font.pixelSize: 10
                        color: logsPopup.autoScroll ? "#4BB34B" : "#76787A"
                    }

                    Text {
                        text: "Автоскролл"
                        font.pixelSize: 11
                        font.bold: logsPopup.autoScroll
                        color: logsPopup.autoScroll ? "#4BB34B" : "#909499"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: logsPopup.autoScroll = !logsPopup.autoScroll
                }
            }

            Text {
                text: "Записей: " + (player.fullLogs === "" ? "0" : player.fullLogs.trim().split("\n").length)
                font.pixelSize: 11
                color: "#76787A"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                height: 32
                Layout.preferredWidth: 90
                radius: 8
                color: "#2688EB"

                Text {
                    anchors.centerIn: parent
                    text: "Закрыть"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: logsPopup.close()
                }
            }
        }
    }
}
