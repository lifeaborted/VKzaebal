import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: authPopup
    anchors.centerIn: parent
    width: Math.min(parent.width * 0.92, 420)
    height: Math.min(parent.height * 0.88, 620)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#212124"
        radius: 16
        border.color: "#303034"
        border.width: 1
    }

    property int vkAuthMode: 0 // 0 = По логину и паролю, 1 = По токену

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        // =====================================================================
        // Заголовок и кнопка закрытия
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true

            SourceBadge {
                sourceName: player.currentSource
                badgeSize: 18
            }

            Text {
                text: "Авторизация: " + player.currentSource
                font.pixelSize: 18
                font.bold: true
                color: "#FFFFFF"
                Layout.fillWidth: true
            }

            Rectangle {
                width: 32
                height: 32
                radius: 16
                color: closeMouse.containsMouse ? "#36363B" : "#2A2A2E"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 16
                    color: "#909499"
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        player.cancelAuth()
                        authPopup.close()
                    }
                }
            }
        }

        // =====================================================================
        // Текущий статус авторизации
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            height: 36
            radius: 8
            color: player.isAuthorized ? "#1B3B2B" : "#3B261B"
            border.color: player.isAuthorized ? "#4BB34B" : "#FFA000"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12

                Text {
                    text: player.isAuthorized ? "✅" : "⚠️"
                    font.pixelSize: 14
                }

                Text {
                    text: player.isAuthorized ? "Аккаунт авторизован" : "Требуется вход в аккаунт"
                    font.pixelSize: 13
                    font.bold: true
                    color: player.isAuthorized ? "#4BB34B" : "#FFA000"
                    Layout.fillWidth: true
                }

                Button {
                    text: "Выйти"
                    visible: player.isAuthorized
                    flat: true
                    onClicked: player.logout(player.currentSource)
                    contentItem: Text {
                        text: "Выйти"
                        color: "#FF5C5C"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        // =====================================================================
        // Плашка ошибки или процесса
        // =====================================================================
        Rectangle {
            Layout.fillWidth: true
            height: statusCol.implicitHeight + 16
            radius: 8
            visible: player.authStep === "authenticating" || player.authStep === "error" || (player.authStatusMessage !== "" && !player.isAuthorized)
            color: player.authStep === "error" ? "#3A1B1B" : "#1B283A"
            border.color: player.authStep === "error" ? "#E64646" : "#2688EB"
            border.width: 1

            ColumnLayout {
                id: statusCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    spacing: 8
                    BusyIndicator {
                        visible: player.authStep === "authenticating"
                        running: player.authStep === "authenticating"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }
                    Text {
                        text: player.authStep === "error" ? "❌ Ошибка" : "ℹ️ Статус"
                        font.bold: true
                        font.pixelSize: 12
                        color: player.authStep === "error" ? "#FF5C5C" : "#5181B8"
                    }
                }

                Text {
                    text: player.authStep === "error" ? player.authError : player.authStatusMessage
                    font.pixelSize: 12
                    color: "#D0D2D6"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 12

                // =============================================================
                // ФОРМА ВХОДА VK
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: player.currentSource === "VK"

                    // Переключатель режима: Логин / Токен
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: vkAuthMode === 0 ? "#2688EB" : "#2A2A2D"

                            Text {
                                anchors.centerIn: parent
                                text: "По логину и паролю"
                                font.pixelSize: 13
                                font.bold: vkAuthMode === 0
                                color: vkAuthMode === 0 ? "#FFFFFF" : "#909499"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: vkAuthMode = 0
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: 8
                            color: vkAuthMode === 1 ? "#2688EB" : "#2A2A2D"

                            Text {
                                anchors.centerIn: parent
                                text: "По токену"
                                font.pixelSize: 13
                                font.bold: vkAuthMode === 1
                                color: vkAuthMode === 1 ? "#FFFFFF" : "#909499"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: vkAuthMode = 1
                            }
                        }
                    }

                    // --- Режим 1: По логину и паролю (нативный VK ID) ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: vkAuthMode === 0

                        // Шаг: Запрос кода 2FA / SMS
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: player.authStep === "need_code"

                            Text {
                                text: "Код подтверждения (SMS / Push / 2FA):"
                                font.pixelSize: 13
                                color: "#E1E3E6"
                            }

                            TextField {
                                id: vkCodeField
                                Layout.fillWidth: true
                                height: 44
                                placeholderText: "Введите 6-значный код"
                                placeholderTextColor: "#76787A"
                                color: "#FFFFFF"
                                font.pixelSize: 15
                                inputMethodHints: Qt.ImhDigitsOnly
                                background: Rectangle {
                                    color: "#2C2D30"
                                    radius: 8
                                    border.color: vkCodeField.activeFocus ? "#2688EB" : "#38393D"
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                height: 44
                                text: "Подтвердить код"
                                enabled: vkCodeField.text.trim().length > 0
                                onClicked: player.submitVkCode(vkCodeField.text.trim())
                                background: Rectangle {
                                    color: parent.enabled ? "#2688EB" : "#1F4E79"
                                    radius: 8
                                }
                                contentItem: Text {
                                    text: "Подтвердить код"
                                    color: "#FFFFFF"
                                    font.bold: true
                                    font.pixelSize: 14
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        // Шаг: Запрос капчи
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: player.authStep === "need_captcha"

                            Text {
                                text: "Введите символы с картинки:"
                                font.pixelSize: 13
                                color: "#E1E3E6"
                            }

                            Image {
                                source: player.authCaptchaImg
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredHeight: 70
                                Layout.preferredWidth: 200
                                fillMode: Image.PreserveAspectFit
                            }

                            TextField {
                                id: vkCaptchaField
                                Layout.fillWidth: true
                                height: 44
                                placeholderText: "Текст с картинки"
                                placeholderTextColor: "#76787A"
                                color: "#FFFFFF"
                                font.pixelSize: 15
                                background: Rectangle {
                                    color: "#2C2D30"
                                    radius: 8
                                    border.color: vkCaptchaField.activeFocus ? "#2688EB" : "#38393D"
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                height: 44
                                text: "Отправить капчу"
                                enabled: vkCaptchaField.text.trim().length > 0
                                onClicked: player.submitVkCaptcha(vkCaptchaField.text.trim())
                                background: Rectangle {
                                    color: parent.enabled ? "#2688EB" : "#1F4E79"
                                    radius: 8
                                }
                                contentItem: Text {
                                    text: "Отправить капчу"
                                    color: "#FFFFFF"
                                    font.bold: true
                                    font.pixelSize: 14
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }

                        // Шаг: Обычный ввод логина и пароля
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: player.authStep !== "need_code" && player.authStep !== "need_captcha"

                            Text {
                                text: "Телефон или Email:"
                                font.pixelSize: 13
                                color: "#E1E3E6"
                            }

                            TextField {
                                id: vkLoginField
                                Layout.fillWidth: true
                                height: 44
                                placeholderText: "+79991234567 или user@mail.ru"
                                placeholderTextColor: "#76787A"
                                color: "#FFFFFF"
                                font.pixelSize: 14
                                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhDialableCharactersOnly
                                background: Rectangle {
                                    color: "#2C2D30"
                                    radius: 8
                                    border.color: vkLoginField.activeFocus ? "#2688EB" : "#38393D"
                                }
                            }

                            Text {
                                text: "Пароль:"
                                font.pixelSize: 13
                                color: "#E1E3E6"
                            }

                            TextField {
                                id: vkPasswordField
                                Layout.fillWidth: true
                                height: 44
                                placeholderText: "Пароль от аккаунта VK"
                                placeholderTextColor: "#76787A"
                                color: "#FFFFFF"
                                font.pixelSize: 14
                                echoMode: TextInput.Password
                                background: Rectangle {
                                    color: "#2C2D30"
                                    radius: 8
                                    border.color: vkPasswordField.activeFocus ? "#2688EB" : "#38393D"
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                height: 44
                                text: player.authStep === "authenticating" ? "Выполняется вход..." : "Войти в ВКонтакте"
                                enabled: vkLoginField.text.trim().length > 0 && player.authStep !== "authenticating"
                                onClicked: player.startVkLogin(vkLoginField.text.trim(), vkPasswordField.text)
                                background: Rectangle {
                                    color: parent.enabled ? "#2688EB" : "#1F4E79"
                                    radius: 8
                                }
                                contentItem: Text {
                                    text: player.authStep === "authenticating" ? "Выполняется вход..." : "Войти в ВКонтакте"
                                    color: "#FFFFFF"
                                    font.bold: true
                                    font.pixelSize: 14
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    // --- Режим 2: По токену ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: vkAuthMode === 1

                        Text {
                            text: "Access Token (Kate Mobile / VK Admin / OAuth):"
                            font.pixelSize: 13
                            color: "#E1E3E6"
                        }

                        TextField {
                            id: vkTokenField
                            Layout.fillWidth: true
                            height: 44
                            placeholderText: "vk1.a... или токен приложения"
                            placeholderTextColor: "#76787A"
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            background: Rectangle {
                                color: "#2C2D30"
                                radius: 8
                                border.color: vkTokenField.activeFocus ? "#2688EB" : "#38393D"
                            }
                        }

                        Text {
                            text: "Secret (необязательно):"
                            font.pixelSize: 13
                            color: "#E1E3E6"
                        }

                        TextField {
                            id: vkSecretField
                            Layout.fillWidth: true
                            height: 44
                            placeholderText: "Секретный ключ (если требуется)"
                            placeholderTextColor: "#76787A"
                            color: "#FFFFFF"
                            font.pixelSize: 13
                            background: Rectangle {
                                color: "#2C2D30"
                                radius: 8
                                border.color: vkSecretField.activeFocus ? "#2688EB" : "#38393D"
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            height: 44
                            text: "Сохранить токен VK"
                            enabled: vkTokenField.text.trim().length > 0
                            onClicked: player.saveToken("VK", vkTokenField.text.trim(), vkSecretField.text.trim())
                            background: Rectangle {
                                color: parent.enabled ? "#2688EB" : "#1F4E79"
                                radius: 8
                            }
                            contentItem: Text {
                                text: "Сохранить токен VK"
                                color: "#FFFFFF"
                                font.bold: true
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                // =============================================================
                // ФОРМА ВХОДА YANDEX
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: player.currentSource === "Yandex"

                    // Основной способ: Авторизация через WebView
                    Button {
                        Layout.fillWidth: true
                        height: 48
                        onClicked: {
                            authPopup.close()
                            player.startWebViewAuth("Yandex")
                        }
                        background: Rectangle {
                            color: "#FFCC00"
                            radius: 8
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            anchors.centerIn: parent
                            ArgonIcon { name: "globe"; size: 16; color: "#19191A" }
                            Text {
                                text: "Войти через Яндекс ID (WebView)"
                                color: "#19191A"
                                font.bold: true
                                font.pixelSize: 14
                            }
                        }
                    }

                    Text {
                        text: "Браузер откроет форму входа Яндекса и автоматически перехватит ваш OAuth-токен."
                        font.pixelSize: 11
                        color: "#909499"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#303034"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    Text {
                        text: "Или введите токен вручную (y0_...):"
                        font.pixelSize: 12
                        color: "#B0B2B6"
                    }

                    TextField {
                        id: yaTokenField
                        Layout.fillWidth: true
                        height: 42
                        placeholderText: "y0_AQAAA..."
                        placeholderTextColor: "#76787A"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#2C2D30"
                            radius: 8
                            border.color: yaTokenField.activeFocus ? "#FFCC00" : "#38393D"
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        height: 40
                        text: "Сохранить токен Яндекс"
                        enabled: yaTokenField.text.trim().length > 0
                        onClicked: player.saveToken("Yandex", yaTokenField.text.trim())
                        background: Rectangle {
                            color: parent.enabled ? "#36363B" : "#242426"
                            radius: 8
                            border.color: parent.enabled ? "#FFCC00" : "transparent"
                        }
                        contentItem: Text {
                            text: "Сохранить токен"
                            color: parent.parent.enabled ? "#FFCC00" : "#808080"
                            font.bold: true
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // =============================================================
                // ФОРМА ВХОДА SOUNDCLOUD
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: player.currentSource === "SoundCloud"

                    // Основной способ: Авторизация через WebView
                    Button {
                        Layout.fillWidth: true
                        height: 48
                        onClicked: {
                            authPopup.close()
                            player.startWebViewAuth("SoundCloud")
                        }
                        background: Rectangle {
                            color: "#FF5500"
                            radius: 8
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            anchors.centerIn: parent
                            ArgonIcon { name: "globe"; size: 16; color: "#FFFFFF" }
                            Text {
                                text: "Войти в SoundCloud (WebView)"
                                color: "#FFFFFF"
                                font.bold: true
                                font.pixelSize: 14
                            }
                        }
                    }

                    Text {
                        text: "Откроется окно входа SoundCloud. Встроенный JS-снайпер автоматически перехватит токен сессии."
                        font.pixelSize: 11
                        color: "#909499"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#303034"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    Text {
                        text: "Или введите токен вручную:"
                        font.pixelSize: 12
                        color: "#B0B2B6"
                    }

                    TextField {
                        id: scTokenField
                        Layout.fillWidth: true
                        height: 42
                        placeholderText: "2-293... или oauth_token"
                        placeholderTextColor: "#76787A"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#2C2D30"
                            radius: 8
                            border.color: scTokenField.activeFocus ? "#FF5500" : "#38393D"
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        height: 40
                        text: "Сохранить токен SoundCloud"
                        enabled: scTokenField.text.trim().length > 0
                        onClicked: player.saveToken("SoundCloud", scTokenField.text.trim())
                        background: Rectangle {
                            color: parent.enabled ? "#36363B" : "#242426"
                            radius: 8
                            border.color: parent.enabled ? "#FF5500" : "transparent"
                        }
                        contentItem: Text {
                            text: "Сохранить токен"
                            color: parent.parent.enabled ? "#FF5500" : "#808080"
                            font.bold: true
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // =============================================================
                // ФОРМА ВХОДА YOUTUBE
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: player.currentSource === "YouTube"

                    // Основной способ: Авторизация через WebView
                    Button {
                        Layout.fillWidth: true
                        height: 48
                        onClicked: {
                            authPopup.close()
                            player.startWebViewAuth("YouTube")
                        }
                        background: Rectangle {
                            color: "#FF0000"
                            radius: 8
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            anchors.centerIn: parent
                            ArgonIcon { name: "globe"; size: 16; color: "#FFFFFF" }
                            Text {
                                text: "Войти через Google (WebView)"
                                color: "#FFFFFF"
                                font.bold: true
                                font.pixelSize: 14
                            }
                        }
                    }

                    Text {
                        text: "Откроется страница Google. После входа сессионные куки YouTube Music (SAPISID) перехватятся автоматически."
                        font.pixelSize: 11
                        color: "#909499"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#303034"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    Text {
                        text: "Или вставьте куки вручную (SID=...; SAPISID=...):"
                        font.pixelSize: 12
                        color: "#B0B2B6"
                    }

                    TextField {
                        id: ytCookieField
                        Layout.fillWidth: true
                        height: 42
                        placeholderText: "Вставьте куки сессии Google/YouTube"
                        placeholderTextColor: "#76787A"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#2C2D30"
                            radius: 8
                            border.color: ytCookieField.activeFocus ? "#FF0000" : "#38393D"
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        height: 40
                        text: "Сохранить куки YouTube"
                        enabled: ytCookieField.text.trim().length > 0
                        onClicked: player.saveToken("YouTube", ytCookieField.text.trim())
                        background: Rectangle {
                            color: parent.enabled ? "#36363B" : "#242426"
                            radius: 8
                            border.color: parent.enabled ? "#FF0000" : "transparent"
                        }
                        contentItem: Text {
                            text: "Сохранить куки"
                            color: parent.parent.enabled ? "#FF5555" : "#808080"
                            font.bold: true
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // =============================================================
                // ФОРМА ВХОДА SPOTIFY
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: player.currentSource === "Spotify"

                    // Основной способ: Авторизация через WebView
                    Button {
                        Layout.fillWidth: true
                        height: 48
                        onClicked: {
                            authPopup.close()
                            player.startWebViewAuth("Spotify")
                        }
                        background: Rectangle {
                            color: "#1DB954"
                            radius: 8
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            anchors.centerIn: parent
                            ArgonIcon { name: "globe"; size: 16; color: "#FFFFFF" }
                            Text {
                                text: "Войти через Spotify (WebView)"
                                color: "#FFFFFF"
                                font.bold: true
                                font.pixelSize: 14
                            }
                        }
                    }

                    Text {
                        text: "Вход с авторизацией по протоколу PKCE. Токен сохранится автоматически."
                        font.pixelSize: 11
                        color: "#909499"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#303034"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    Text {
                        text: "Или введите SP_DC cookie / токен вручную:"
                        font.pixelSize: 12
                        color: "#B0B2B6"
                    }

                    TextField {
                        id: spTokenField
                        Layout.fillWidth: true
                        height: 42
                        placeholderText: "AQD... или токен сессии"
                        placeholderTextColor: "#76787A"
                        color: "#FFFFFF"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#2C2D30"
                            radius: 8
                            border.color: spTokenField.activeFocus ? "#1DB954" : "#38393D"
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        height: 40
                        text: "Сохранить токен Spotify"
                        enabled: spTokenField.text.trim().length > 0
                        onClicked: player.saveToken("Spotify", spTokenField.text.trim())
                        background: Rectangle {
                            color: parent.enabled ? "#36363B" : "#242426"
                            radius: 8
                            border.color: parent.enabled ? "#1DB954" : "transparent"
                        }
                        contentItem: Text {
                            text: "Сохранить токен"
                            color: parent.parent.enabled ? "#1DB954" : "#808080"
                            font.bold: true
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
