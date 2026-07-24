import QtQuick
import MeetUp


// Reproduces anon_lobby.html: join a room by code or create a broadcast,
// without an account. A "Войти" link (top bar) leads to the login page.
AuthScaffold {
    id: root

    // Подстановка из ссылки-приглашения (Link -> Main.qml). notice объясняет,
    // почему мы вдруг на другом сервере и откуда взялось имя.
    property string prefillCode: ""
    property string prefillName: ""
    property string noticeText: ""

    showSignIn: true
    heroTitle: "Встреча<br/>за <font color='" + Theme.accentInk + "'>один клик</font>"
    heroSub: "Без установки и регистрации. Укажите имя, введите код комнаты — и вы в эфире."
    heroMeta: "Открытая видеосвязь"

    Component.onCompleted: {
        if (prefillCode !== "") roomInput.text = prefillCode
        if (prefillName !== "") nameInput.text = prefillName
        // Фокус — в то поле, которое осталось пустым: одно движение до входа.
        if (nameInput.text === "") nameInput.forceActiveFocus()
        else if (roomInput.text === "") roomInput.forceActiveFocus()
    }

    // Плашка «сервер переключён» — над формой, в тон карточки.
    Rectangle {
        visible: root.noticeText !== ""
        width: parent.width
        implicitHeight: noticeCol.implicitHeight + 24
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: Theme.border

        Column {
            id: noticeCol
            x: 12; y: 12
            width: parent.width - 24
            spacing: 6
            Row {
                spacing: 8
                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "link"; size: 14; color: Theme.accentInk
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "СЕРВЕР ПЕРЕКЛЮЧЁН"
                    color: Theme.accentInk
                    font.family: Theme.labelFont
                    font.pixelSize: Theme.text2xs
                    font.letterSpacing: 2
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.noticeText
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
            Text {   // заглушка E2E: ключ из ссылки есть, шифровать нечем
                visible: Link.pendingKey !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: "В ссылке есть ключ шифрования. Десктоп-клиент пока не умеет E2E — "
                      + "войдёте без него, чужие сообщения и видео будут нечитаемы."
                color: Theme.danger
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
        }
    }

    Field {
        width: parent.width
        label: "Ваше имя"
        AppInput { id: nameInput; width: parent.width; placeholderText: "Анна" }
    }
    Field {
        width: parent.width
        label: "Код комнаты"
        hint: "Спросите код у организатора"
        AppInput {
            id: roomInput
            width: parent.width
            placeholderText: "Например: fRt7…"
            onAccepted: Rooms.joinByCode(roomInput.text, nameInput.text)   // Enter в поле
            onTextChanged: Rooms.clearError()                              // печатает — гасим ошибку
        }
    }

    AppButton {
        width: parent.width
        text: "Войти в конференцию"
        variant: "primary"
        iconRight: "arrow-right"
        enabled: !Rooms.busy
        onClicked: Rooms.joinByCode(roomInput.text, nameInput.text)
    }

    Divider { width: parent.width; label: "или" }

    AppButton {
        width: parent.width
        text: "Создать трансляцию"
        variant: "ghost"
        icon: "plus"
        enabled: !Rooms.busy
        onClicked: Rooms.createRoom(nameInput.text)
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: Rooms.errorText !== "" ? Rooms.errorText
                                     : "Код новой комнаты сгенерирует сервер."
        color: Rooms.errorText !== "" ? Theme.danger : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
