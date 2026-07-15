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
                model: MockData.messages
                boundsBehavior: Flickable.StopAtBounds
                delegate: ChatMessage {
                    required property var modelData
                    width: chatList.width
                    author: modelData.author
                    text: modelData.text
                    time: modelData.time
                    self: modelData.self
                }
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
                    IconButton { size: "sm"; icon: "paperclip"; variant: "neutral" }
                    AppInput {
                        Layout.fillWidth: true
                        placeholderText: "Сообщение…"
                    }
                    IconButton { size: "sm"; icon: "send"; variant: "accent" }
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
            model: MockData.participants
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
                    text: root.roomCode !== "" ? root.roomCode : MockData.roomTitle
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    font.letterSpacing: -0.8
                }
                Badge { tone: "live"; dot: true; text: "в эфире · 05:14" }
                Badge { tone: "muted"; text: "38 ms" }
                Badge { tone: "accent"; text: "🔒 E2E" }

                Item { Layout.fillWidth: true }

                IconButton { size: "sm"; icon: "copy"; variant: "neutral" }
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
            columns: Math.max(1, Math.ceil(Math.sqrt(MockData.participants.length)))

            Repeater {
                model: MockData.participants
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
            onToggleMic: root.micOn = !root.micOn
            onToggleCam: root.camOn = !root.camOn
            onToggleShare: root.sharing = !root.sharing
            onLeave: root.leaveRequested()
        }
    }
}
