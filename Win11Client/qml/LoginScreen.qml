import QtQuick
import MeetUp

// Reproduces login.html: sign in, create account, or continue anonymously.
// «Войти» ходит на сервер через Auth (инструкция №1); остальные кнопки — навигация.
AuthScaffold {
    id: root

    signal registerRequested()
    signal anonRequested()

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
            onAccepted: Auth.login(loginInput.text, passInput.text)
        }
    }

    AppButton {
        width: parent.width
        text: "Войти"
        variant: "primary"
        iconRight: "arrow-right"
        enabled: !Auth.busy
        onClicked: Auth.login(loginInput.text, passInput.text)
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
        text: Auth.errorText !== "" ? Auth.errorText
                                : "Аккаунт хранит ваше имя между встречами."
        color: Auth.errorText !== "" ? Theme.danger : Theme.textFaint   // <— красный при ошибке
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Адрес сервера: по умолчанию прод, правка сохраняется между запусками.
    Field {
        width: parent.width
        label: "Сервер"
        hint: "Пусто — вернуть адрес по умолчанию."
        AppInput {
            id: serverInput
            width: parent.width
            text: Sys.serverAddress
            placeholderText: "meetup.linkpc.net"
            // Enter или уход фокуса — применяем и показываем нормализованный вид.
            onEditingFinished: {
                if (text !== Sys.serverAddress) {
                    Sys.setServer(text)
                    text = Sys.serverAddress
                    Auth.checkSession()   // вдруг на этом сервере жива сессия — сразу впустит
                }
            }
        }
    }
}
