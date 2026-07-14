import QtQuick
import QtQuick.Layouts
import MeetUp

// Reproduces register.html: create an account (login, display name, password).
AuthScaffold {
    id: root

    signal registerSubmitted(string login, string name, string password)
    // signInRequested is inherited from AuthScaffold.

    property string err: ""

    heroTitle: "Свой аккаунт —<br/>своя <font color='" + Theme.accentInk + "'>комната</font>"
    heroSub: "Логин и пароль — для входа. Отображаемое имя увидят собеседники, его можно поменять в любой момент."

    Field {
        width: parent.width
        label: "Логин"
        AppInput { id: loginInput; width: parent.width; placeholderText: "anna" }
    }
    Field {
        width: parent.width
        label: "Отображаемое имя"
        AppInput { id: nameInput; width: parent.width; placeholderText: "Анна" }
    }
    Field {
        width: parent.width
        label: "Пароль"
        hint: "Минимум 8 символов"
        AppInput { id: passInput; width: parent.width; isPassword: true; placeholderText: "••••••••" }
    }
    Field {
        width: parent.width
        label: "Повторите пароль"
        AppInput {
            id: pass2Input
            width: parent.width
            isPassword: true
            placeholderText: "••••••••"
            onAccepted: root.registerSubmitted(loginInput.text, nameInput.text, passInput.text)
        }
    }

    AppButton {
        width: parent.width
        text: "Создать аккаунт"
        variant: "primary"
        iconRight: "arrow-right"
        onClicked: root.registerSubmitted(loginInput.text, nameInput.text, passInput.text)
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
        text: root.err !== "" ? root.err : "Пароль хранится только в виде хеша."
        color: root.err !== "" ? Theme.danger : Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
