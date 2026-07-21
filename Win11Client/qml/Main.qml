import QtQuick
import QtQuick.Window
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

    // Полноэкранный режим. Прежнее состояние запоминаем: окно могло быть
    // развёрнутым, и возвращать его «в окно» было бы обидно.
    readonly property bool fullScreen: visibility === Window.FullScreen
    property int _prevVisibility: Window.Windowed

    function toggleFullScreen() {
        if (window.fullScreen) {
            window.visibility = window._prevVisibility
        } else {
            window._prevVisibility = window.visibility
            window.visibility = Window.FullScreen
        }
    }

    Shortcut { sequences: ["F11"]; onActivated: window.toggleFullScreen() }
    Shortcut {
        sequences: ["Esc"]
        enabled: window.fullScreen      // вне полного экрана Esc не наш
        onActivated: window.toggleFullScreen()
    }

    // Account flow: login <-> register / anon-lobby -> home -> conference.
    // Peer screens use replace(); the conference is push()ed so leaving pops back.
    StackView {
        id: stack
        anchors.fill: parent
        initialItem: loginPage

        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        pushEnter:    Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
        popEnter:     Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durMed } }
    }

    // Вход/выход из аккаунта (и обычный login, и авто-вход, и logout).
    Connections {
        target: Auth
        function onLoggedIn() {
            // Если мы уже на home — не дублируем.
            if (stack.currentItem && stack.currentItem.objectName === "home") return;
            stack.replace(homePage);
        }
        function onLoggedOut() {
            MyRoom.reset();               // чужая комната не должна мигнуть следующему
            stack.replace(loginPage);     // выход всегда ведёт на форму входа
        }
    }

    // Авто-вход при старте: спрашиваем /api/me один раз.
    Component.onCompleted: Auth.checkSession()

    // Комната готова (создана/проверена/доверенная) — открываем конференцию,
    // передавая код и имя как свойства нового экрана.
    Connections {
        target: Rooms
        function onRoomReady(code, name) {
            stack.push(confPage, { roomCode: code, myName: name });
        }
    }

    // Успешный вход в комнату — пополняем локальную историю для главной.
    Connections {
        target: Conf
        function onJoinedRoom(code, title) { History.record(code, title) }
    }

    Component {
        id: loginPage
        LoginScreen {
            onRegisterRequested: () => { Auth.clearError(); stack.replace(registerPage) }
            onAnonRequested: () => stack.replace(anonPage)
        }
    }

    Component {
        id: registerPage
        RegisterScreen {
            onSignInRequested: () => { Auth.clearError(); stack.replace(loginPage) }
        }
    }

    Component {
        id: anonPage
        AnonLobbyScreen {
            onSignInRequested: () => stack.replace(loginPage)
        }
    }

    Component {
        id: homePage
        HomeScreen {}
    }

    Component {
        id: confPage
        ConferenceScreen {
            onLeaveRequested: stack.pop()
            onFullScreenRequested: window.toggleFullScreen()
        }
    }
}
