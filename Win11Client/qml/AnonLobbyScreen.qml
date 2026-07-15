import QtQuick
import MeetUp


// Reproduces anon_lobby.html: join a room by code or create a broadcast,
// without an account. A "Войти" link (top bar) leads to the login page.
AuthScaffold {
    id: root

    showSignIn: true
    heroTitle: "Встреча<br/>за <font color='" + Theme.accentInk + "'>один клик</font>"
    heroSub: "Без установки и регистрации. Укажите имя, введите код комнаты — и вы в эфире."
    heroMeta: "Открытая видеосвязь"

    Field {
        width: parent.width
        label: "Ваше имя"
        AppInput { id: nameInput; width: parent.width; placeholderText: "Анна"; text: "Игорь" }
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
