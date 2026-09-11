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

    // Регистрация закрыта: форму не показываем вовсе. Вместо неё — объяснение
    // и дорога обратно; заполнять поля, которые сервер отвергнет, незачем.
    Column {
        width: parent.width
        spacing: 16
        visible: !Server.registration

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Регистрация закрыта"
            color: Theme.text
            font.family: Theme.displayFont
            font.pixelSize: Theme.textXl
            font.weight: Font.DemiBold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Владелец этого сервера не принимает новых участников. "
                  + "Если вам выдали логин и пароль — войдите с ними."
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        AppButton {
            width: parent.width
            text: "Ко входу"
            variant: "primary"
            iconRight: "arrow-right"
            onClicked: root.signInRequested()
        }
    }

    Field {
        width: parent.width
        visible: Server.registration
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
        visible: Server.registration
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
        visible: Server.registration
        label: "Пароль"
        // Правило длины — серверное: показываем то, что он и потребует.
        hint: "Минимум " + Server.minPasswordLen + " символов"
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
        visible: Server.registration
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
        visible: Server.registration
        text: "Создать аккаунт"
        variant: "primary"
        iconRight: "arrow-right"
        enabled: !Auth.busy
        onClicked: Auth.registerAccount(loginInput.text, nameInput.text,
                                        passInput.text, pass2Input.text)
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: Server.registration
        spacing: 5
        Text {
            text: "Уже есть аккаунт?"
            color: Theme.textFaint
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
        TextLink {
            text: "Войти"
            onClicked: root.signInRequested()
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
