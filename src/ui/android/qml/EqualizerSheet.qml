import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    x: 0
    y: parent.height - height
    width: parent.width
    height: Math.min(parent.height * 0.72, 510)
    padding: 0
    z: 1000
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property color colBgSurface: "#121218"
    readonly property color colBorderShell: "#14FFFFFF"
    readonly property color colAccentGreen: "#47B881"
    readonly property color colTextPrimary: "#FFFFFF"
    readonly property color colTextSecondary: "#9E9EA8"
    readonly property color colTextSubdued: "#5C5C66"

    property var bandFrequencies: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    property var bandGains: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property string activePreset: player.equalizerPreset || "Flat"

    function refreshGains() {
        var gains = player.getEqualizerBands()
        if (gains && gains.length >= 10) {
            bandGains = gains
        }
        activePreset = player.equalizerPreset
    }

    onOpened: refreshGains()

    background: Rectangle {
        color: root.colBgSurface
        radius: 24
        border.color: root.colBorderShell
    }

    enter: Transition {
        NumberAnimation { property: "y"; from: parent.height; to: parent.height - root.height; duration: 240; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "y"; from: parent.height - root.height; to: parent.height; duration: 200; easing.type: Easing.InCubic }
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // Handle
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 36
            height: 4
            radius: 2
            color: "#26FFFFFF"
        }

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: "10-полосный эквалайзер"
                    font.pixelSize: 17
                    font.bold: true
                    color: root.colTextPrimary
                }

                Text {
                    text: "DSP Biquad фильтры 32-bit Float"
                    font.pixelSize: 11
                    font.family: "JetBrains Mono"
                    color: player.isEqualizerEnabled ? root.colAccentGreen : root.colTextSubdued
                }
            }

            Switch {
                id: eqSwitch
                checked: player.isEqualizerEnabled
                onToggled: player.setEqualizerEnabled(checked)
            }

            Rectangle {
                width: 32
                height: 32
                radius: 16
                color: "#14FFFFFF"

                ArgonIcon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 14
                    color: root.colTextSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.close()
                }
            }
        }

        // Preset Chips
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            contentWidth: presetRow.implicitWidth + 8
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            RowLayout {
                id: presetRow
                spacing: 8
                height: parent.height

                Repeater {
                    model: [
                        { id: "Flat", name: "Прямой (Flat)" },
                        { id: "Bass Boost", name: "Усиление баса" },
                        { id: "Rock", name: "Рок" },
                        { id: "Vocal Boost", name: "Вокал" },
                        { id: "Electronic", name: "Электроника" },
                        { id: "Classical", name: "Классика" }
                    ]

                    delegate: Rectangle {
                        height: 28
                        width: chipTxt.implicitWidth + 20
                        radius: 14
                        color: root.activePreset === modelData.id ? "#FFFFFF" : "#14FFFFFF"
                        border.color: root.activePreset === modelData.id ? "#FFFFFF" : root.colBorderShell

                        Text {
                            id: chipTxt
                            anchors.centerIn: parent
                            text: modelData.name
                            font.pixelSize: 11
                            font.bold: root.activePreset === modelData.id
                            color: root.activePreset === modelData.id ? "#09090C" : root.colTextSecondary
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.activePreset = modelData.id
                                player.applyPreset(modelData.id)
                                root.refreshGains()
                            }
                        }
                    }
                }
            }
        }

        // Sliders Row (10 Vertical Sliders)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2

            Repeater {
                model: 10

                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    Text {
                        text: (Math.round(root.bandGains[modelData]) > 0 ? "+" : "") + Math.round(root.bandGains[modelData]) + "dB"
                        font.pixelSize: 9
                        font.family: "JetBrains Mono"
                        color: Math.abs(root.bandGains[modelData]) > 0.1 ? root.colAccentGreen : root.colTextSubdued
                        Layout.alignment: Qt.AlignHCenter
                    }

                    // Vertical Slider
                    Slider {
                        id: bandSlider
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        from: -12.0
                        to: 12.0
                        stepSize: 0.5
                        value: root.bandGains[modelData] !== undefined ? root.bandGains[modelData] : 0.0

                        background: Rectangle {
                            x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                            y: bandSlider.topPadding
                            implicitWidth: 4
                            implicitHeight: 180
                            width: implicitWidth
                            height: bandSlider.availableHeight
                            radius: 2
                            color: "#1E1E28"

                            Rectangle {
                                width: parent.width
                                height: Math.abs(bandSlider.visualPosition - 0.5) * parent.height
                                y: Math.min(bandSlider.visualPosition, 0.5) * parent.height
                                color: root.colAccentGreen
                                radius: 2
                            }
                        }

                        handle: Rectangle {
                            x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                            y: bandSlider.topPadding + bandSlider.visualPosition * (bandSlider.availableHeight - height)
                            implicitWidth: 14
                            implicitHeight: 14
                            radius: 7
                            color: "#FFFFFF"
                            border.color: root.colAccentGreen
                            border.width: 2
                        }

                        onMoved: {
                            var g = Math.round(value * 2) / 2
                            player.setEqualizerBand(modelData, g)
                            var copy = root.bandGains.slice()
                            copy[modelData] = g
                            root.bandGains = copy
                            root.activePreset = "Custom"
                        }
                    }

                    Text {
                        text: root.bandFrequencies[modelData]
                        font.pixelSize: 10
                        font.family: "JetBrains Mono"
                        color: root.colTextSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }

        // Bottom Action Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                height: 38
                radius: 10
                color: "#1AFFFFFF"

                Text {
                    anchors.centerIn: parent
                    text: "Сброс в 0 dB"
                    font.pixelSize: 12
                    font.bold: true
                    color: root.colTextSecondary
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        player.resetEqualizer()
                        root.refreshGains()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 38
                radius: 10
                color: root.colAccentGreen

                Text {
                    anchors.centerIn: parent
                    text: "Применить"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#09090C"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.close()
                }
            }
        }
    }
}
