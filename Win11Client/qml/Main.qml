import QtQuick
import QtQuick.Controls.Basic
import MeetUp

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    minimumWidth: 760
    minimumHeight: 540
    visible: true
    title: "MeetUp"
    color: Theme.bg

    Behavior on color { ColorAnimation { duration: Theme.durMed } }

    // Account flow: login <-> register / anon-lobby -> home -> conference.
    // Peer screens use replace(); the conference is push()ed so leaving pops back.
    // With networking (M1+) these transitions are driven by server responses.
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: loginPage

        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        pushEnter:    Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        popEnter:     Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
    }

    // Реакция на успешный вход (из любого места: и обычный login, и авто-вход).
    Connections {
        target: Auth
        function onLoggedIn() {
            // Если мы уже на home — не дублируем.
            if (stack.currentItem && stack.currentItem.objectName === "home") return;
            stack.replace(homePage);
        }
    }

    // Авто-вход при старте: спрашиваем /api/me один раз.
    Component.onCompleted: Auth.checkSession()

    Component {
        id: loginPage
        LoginScreen {
            onLoginRequested: (login, password) => stack.replace(homePage)
            onRegisterRequested: () => stack.replace(registerPage)
            onAnonRequested: () => stack.replace(anonPage)
        }
    }

    Component {
        id: registerPage
        RegisterScreen {
            onRegisterSubmitted: (login, name, password) => stack.replace(homePage)
            onSignInRequested: () => stack.replace(loginPage)
        }
    }

    Component {
        id: anonPage
        AnonLobbyScreen {
            onJoinRequested: (code, name) => stack.push(confPage)
            onCreateRequested: (name) => stack.push(confPage)
            onSignInRequested: () => stack.replace(loginPage)
        }
    }

    Component {
        id: homePage
        HomeScreen {
            onLogoutRequested: () => stack.replace(loginPage)
            onOpenConference: (code) => stack.push(confPage)
        }
    }

    Component {
        id: confPage
        ConferenceScreen {
            onLeaveRequested: stack.pop()
        }
    }
}
