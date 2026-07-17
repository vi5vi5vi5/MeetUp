import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import MeetUp

// Conference view: stage (top bar · adaptive tile grid · floating dock) plus a
// right-hand chat/participants panel. All state is local mock — no networking.
Item {
    id: root

    signal leaveRequested()

    property string roomCode: ""
    property string myName: ""

    property bool micOn: true
    property bool camOn: true
    property bool sharing: false
    property int activeTab: 0   // 0 = chat, 1 = participants
    readonly property bool showSide: width > 760

    // Честный бейдж эфира: считаем от момента СВОЕГО входа (начало эфира
    // разовой комнаты серверу неизвестно — вебу, впрочем, тоже).
    property bool linkCopied: false
    property double joinedAtMs: 0
    property double nowMs: Date.now()

    // «05:14», после часа — «1:05:14» (как на главной)
    function fmtDuration(ms) {
        var s = Math.max(0, Math.floor(ms / 1000))
        function two(n) { return (n < 10 ? "0" : "") + n }
        var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60
        return (h ? h + ":" + two(m) : two(m)) + ":" + two(s % 60)
    }

    Timer {
        interval: 1000; repeat: true
        running: Conf.phase === "live"
        onTriggered: root.nowMs = Date.now()
    }
    Connections {
        target: Conf
        function onPhaseChanged() {
            if (Conf.phase === "live" && root.joinedAtMs === 0) root.joinedAtMs = Date.now()
        }
    }
    Timer { id: linkCopyReset; interval: 1400; onTriggered: root.linkCopied = false }

    Component.onCompleted: Conf.open(root.roomCode, root.myName)
    Component.onDestruction: Conf.leave()

    // ---------------------------------------------------------------- Side panel
    Rectangle {
        id: side
        visible: root.showSide
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 340
        color: Theme.surface

        Rectangle {
            anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left
            width: 1; color: Theme.border
        }

        // Tabs
        Row {
            id: tabs
            height: 60
            anchors { top: parent.top; left: parent.left; right: parent.right }
            anchors.leftMargin: Theme.padPanel
            anchors.rightMargin: Theme.padPanel
            spacing: 8

            Repeater {
                model: [ { label: "Чат", idx: 0 }, { label: "Участники", idx: 1 } ]
                delegate: Rectangle {
                    required property var modelData
                    anchors.verticalCenter: parent.verticalCenter
                    width: tabLabel.implicitWidth + 28
                    height: 34
                    radius: Theme.radiusPill
                    color: root.activeTab === modelData.idx ? Theme.accent : "transparent"
                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.activeTab === modelData.idx ? Theme.accentFg : Theme.textMuted
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Bold
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = modelData.idx
                    }
                }
            }
        }
        Rectangle {
            anchors.top: tabs.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 1; color: Theme.border
        }

        // ---- Chat tab ----
        Item {
            visible: root.activeTab === 0
            anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

            ListView {
                id: chatList
                anchors { top: parent.top; left: parent.left; right: parent.right; bottom: composer.top }
                anchors.margins: Theme.padPanel
                clip: true
                spacing: 12
                model: Conf.messages
                boundsBehavior: Flickable.StopAtBounds
                delegate: ChatMessage {
                    required property var modelData
                    width: chatList.width
                    author: modelData.author
                    text: modelData.text
                    time: modelData.time
                    self: modelData.self
                }
                // держим ленту прокрученной вниз при новом сообщении/истории
                onCountChanged: positionViewAtEnd()
                Component.onCompleted: positionViewAtEnd()
            }

            Rectangle {
                id: composer
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: composerRow.implicitHeight + 24
                color: Theme.surface
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    height: 1; color: Theme.border
                }

                RowLayout {
                    id: composerRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    IconButton { size: "sm"; icon: "paperclip"; variant: "neutral"; enabled: false }  // картинки — M5
                    AppInput {
                        id: chatInput
                        Layout.fillWidth: true
                        placeholderText: "Сообщение…"
                        enabled: Conf.phase === "live"
                        onAccepted: { Conf.sendChat(chatInput.text); chatInput.text = ""; }
                    }
                    IconButton {
                        size: "sm"; icon: "send"; variant: "accent"
                        enabled: Conf.phase === "live"
                        onClicked: { Conf.sendChat(chatInput.text); chatInput.text = ""; }
                    }
                }
            }
        }

        // ---- Participants tab ----
        ListView {
            visible: root.activeTab === 1
            anchors { top: tabs.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.margins: 10
            clip: true
            spacing: 2
            model: Conf.participants
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                required property var modelData
                width: ListView.view.width
                height: 52
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSm
                    color: hov.hovered ? Theme.surface2 : "transparent"
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 10
                    Rectangle {
                        width: 34; height: 34; radius: 17
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.surface3 }
                            GradientStop { position: 1.0; color: Theme.surface }
                        }
                        border.width: 1; border.color: Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: {
                                var p = String(modelData.name).trim().split(/\s+/)
                                return (p[0] ? p[0][0] : "?").toUpperCase()
                            }
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name + (modelData.isSelf ? "  · вы" : "")
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.textSm
                        font.weight: Font.Medium
                    }
                    AppIcon {
                        name: modelData.mic ? "mic" : "mic-off"
                        size: 16
                        color: modelData.mic ? Theme.textMuted : Theme.danger
                    }
                }
                HoverHandler { id: hov }
            }
        }
    }

    // ------------------------------------------------------------------- Stage
    Item {
        id: stage
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        anchors.right: root.showSide ? side.left : parent.right

        // Top bar
        Item {
            id: confbar
            height: 68
            anchors { top: parent.top; left: parent.left; right: parent.right }
            anchors.leftMargin: Theme.padStage
            anchors.rightMargin: Theme.padStage

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Text {
                    text: Conf.roomTitle !== "" ? Conf.roomTitle
                        : (root.roomCode !== "" ? root.roomCode : MockData.roomTitle)
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    font.letterSpacing: -0.8
                }
                Badge {
                    visible: Conf.phase === "live"
                    tone: "live"; dot: true
                    text: "в эфире · " + root.fmtDuration(root.nowMs - root.joinedAtMs)
                }
                Badge { visible: false; tone: "muted"; text: "38 ms" }    // вернём в M8 (ping/RTT)
                Badge { visible: false; tone: "accent"; text: "🔒 E2E" }  // вернём в M5 (шифрование)

                Item { Layout.fillWidth: true }

                IconButton { // поделиться ссылкой на комнату
                    size: "sm"
                    icon: root.linkCopied ? "check" : "copy"
                    variant: root.linkCopied ? "active" : "neutral"
                    onClicked: {
                        Sys.copyText(Sys.roomLink(root.roomCode))
                        root.linkCopied = true
                        linkCopyReset.restart()
                    }
                }
                IconButton { size: "sm"; icon: Theme.dark ? "sun" : "moon"; variant: "neutral"; onClicked: Theme.toggle() }
            }
        }

        // Tile grid
        GridLayout {
            id: grid
            anchors { top: confbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            anchors.leftMargin: Theme.padStage
            anchors.rightMargin: Theme.padStage
            anchors.topMargin: 4
            anchors.bottomMargin: 116   // clear the floating dock
            columnSpacing: Theme.gapGrid
            rowSpacing: Theme.gapGrid
            columns: Math.max(1, Math.ceil(Math.sqrt(Conf.participants.length)))

            Repeater {
                model: Conf.participants // было: MockData.participants
                delegate: VideoTile {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    name: modelData.name
                    isSelf: modelData.isSelf
                    speaking: modelData.speaking
                    mic: modelData.isSelf ? root.micOn : modelData.mic
                    cam: modelData.isSelf ? root.camOn : modelData.cam
                }
            }
        }

        // Floating dock
        ControlDock {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            micOn: root.micOn
            camOn: root.camOn
            sharing: root.sharing
            onToggleMic: { root.micOn = !root.micOn; Conf.setLocalState(root.micOn, root.camOn); }
            onToggleCam: { root.camOn = !root.camOn; Conf.setLocalState(root.micOn, root.camOn); }
            onToggleShare: root.sharing = !root.sharing
            onLeave: root.leaveRequested()
        }

        // Баннер «идёт переподключение»: связь оборвалась, попытки продолжаются
        // (phase остаётся "live", поэтому оверлей ниже не показывается).
        Badge {
            visible: Conf.reconnecting
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 74
            z: 60
            tone: "danger"
            dot: true
            text: "переподключение…"
        }

        Rectangle {
            anchors.fill: parent
            visible: Conf.phase !== "live"
            color: Theme.bg
            z: 50

            Column {
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 48)
                spacing: 16

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    text: Conf.phase === "connecting" ? "Подключение…"
                        : Conf.phase === "waiting"    ? (Conf.roomTitle || "Комната") + " ещё не в эфире"
                        : Conf.phase === "gate"       ? "Комната защищена паролем"
                        : /* error */                   "Не удалось войти"
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Conf.errorText !== "" ? Theme.danger : Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textSm
                    visible: text !== ""
                    text: Conf.errorText !== "" ? Conf.errorText
                        : Conf.phase === "waiting" ? "Ждём, пока владелец откроет комнату. Мы войдём автоматически."
                        : ""
                }

                // Гейт пароля
                Field {
                    visible: Conf.phase === "gate"
                    width: parent.width
                    label: "Пароль комнаты"
                    AppInput {
                        id: gatePass
                        width: parent.width
                        isPassword: true
                        placeholderText: "••••••"
                        onAccepted: Conf.submitPassword(gatePass.text)
                    }
                }
                AppButton {
                    visible: Conf.phase === "gate"
                    width: parent.width
                    text: "Войти"
                    variant: "primary"
                    onClicked: Conf.submitPassword(gatePass.text)
                }

                // Фатальная ошибка — только выйти
                AppButton {
                    visible: Conf.phase === "error"
                    width: parent.width
                    text: "Назад"
                    variant: "secondary"
                    onClicked: root.leaveRequested()
                }
            }
        }
    }
}
