import QtQuick
import MeetUp

// Reproduces login.html: sign in, create account, or continue anonymously.
// Buttons emit navigation signals; the real auth logic is added later
// (see docs/account-login-guide.md).
AuthScaffold {
    id: root

    signal loginRequested(string login, string password)
    signal registerRequested()
    signal anonRequested()

    property string err: ""

    heroTitle: "С возвращением<br/>в <font color='" + Theme.accentInk + "'>эфир</font>"
    heroSub: "Войдите в аккаунт, чтобы продолжить встречи, историю комнат и контакты."

    Field {
        width: parent.width
        label: "Логин или email"
        AppInput {
            id: loginInput
            width: parent.width
            placeholderText: "anna@meetup.dev"
        }
    }
    Field {
        width: parent.width
        label: "Пароль"
        AppInput {
            id: passInput
            width: parent.width
            isPassword: true
            placeholderText: "••••••••"
            onAccepted: root.loginRequested(loginInput.text, passInput.text)
        }
    }

    AppButton {
        width: parent.width
        text: "Войти"
        variant: "primary"
        iconRight: "arrow-right"
        onClicked: root.loginRequested(loginInput.text, passInput.text)
    }
    AppButton {
        width: parent.width
        text: "Создать аккаунт"
        variant: "ghost"
        icon: "plus"
        onClicked: root.registerRequested()
    }

    Divider { width: parent.width; label: "или" }

    AppButton {
        width: parent.width
        text: "Войти без аккаунта"
        variant: "secondary"
        icon: "arrow-right"
        onClicked: root.anonRequested()
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: root.err !== "" ? root.err : "Аккаунт хранит ваше имя между встречами."
        color: root.err !== "" ? Theme.danger : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
