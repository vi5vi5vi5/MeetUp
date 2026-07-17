import QtQuick
import QtQuick.Layouts
import MeetUp

// Reproduces register.html: create an account (login, display name, password).
// Кнопка зовёт Auth.registerAccount — валидация и тексты ошибок в контроллере.
AuthScaffold {
    id: root

    // signInRequested is inherited from AuthScaffold.

    heroTitle: "Свой аккаунт —<br/>своя <font color='" + Theme.accentInk + "'>комната</font>"
    heroSub: "Логин и пароль — для входа. Отображаемое имя увидят собеседники, его можно поменять в любой момент."

    Field {
        width: parent.width
        label: "Логин"
        AppInput {
            id: loginInput
            width: parent.width
            placeholderText: "anna"
            maximumLength: 32                          // как maxLength у веба
            onTextChanged: Auth.clearError()
        }
    }
    Field {
        width: parent.width
        label: "Отображаемое имя"
        AppInput {
            id: nameInput
            width: parent.width
            placeholderText: "Анна"
            maximumLength: 40
            onTextChanged: Auth.clearError()
        }
    }
    Field {
        width: parent.width
        label: "Пароль"
        hint: "Минимум 8 символов"
        AppInput {
            id: passInput
            width: parent.width
            isPassword: true
            placeholderText: "••••••••"
            maximumLength: 128
            onTextChanged: Auth.clearError()
        }
    }
    Field {
        width: parent.width
        label: "Повторите пароль"
        AppInput {
            id: pass2Input
            width: parent.width
            isPassword: true
            placeholderText: "••••••••"
            maximumLength: 128
            onTextChanged: Auth.clearError()
            onAccepted: Auth.registerAccount(loginInput.text, nameInput.text,
                                             passInput.text, pass2Input.text)   // Enter
        }
    }

    AppButton {
        width: parent.width
        text: "Создать аккаунт"
        variant: "primary"
        iconRight: "arrow-right"
        enabled: !Auth.busy
        onClicked: Auth.registerAccount(loginInput.text, nameInput.text,
                                        passInput.text, pass2Input.text)
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5
        Text {
            text: "Уже есть аккаунт?"
            color: Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
        Text {
            text: "Войти"
            color: Theme.accentInk
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
            font.weight: Font.DemiBold
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.signInRequested() }
        }
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: Auth.errorText !== "" ? Auth.errorText : "Пароль хранится только в виде хеша."
        color: Auth.errorText !== "" ? Theme.danger : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
