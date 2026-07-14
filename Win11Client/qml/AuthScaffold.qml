import QtQuick
import QtQuick.Layouts
import MeetUp

// Shared layout for the login / register / anon-lobby pages: a top bar, then a
// centered two-column "stage" — hero text on the left, a card on the right.
// Reproduces the web pages (login.html / register.html / anon_lobby.html).
// The card body is provided by the caller as child content.
Item {
    id: root

    // Rich-text hero headline (may contain <br/> and a <font color=accentInk>).
    property string heroTitle: ""
    property string heroSub: ""
    property string heroMeta: "Открытая видеосвязь без установки"
    property bool showSignIn: false          // anon-lobby shows a "Войти" link

    signal signInRequested()

    default property alias cardContent: card.content
    readonly property bool wide: width > 900

    // ---- Top bar ----
    Item {
        id: topbar
        height: 64
        anchors { top: parent.top; left: parent.left; right: parent.right }
        anchors.leftMargin: Theme.padStage
        anchors.rightMargin: Theme.padStage

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "MEETUP"
            color: Theme.text
            font.family: Theme.labelFont
            font.pixelSize: Theme.textLg
            font.letterSpacing: 3
            font.weight: Font.Bold
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            spacing: 16
            Text {
                visible: root.showSignIn
                anchors.verticalCenter: parent.verticalCenter
                text: "ВОЙТИ"
                color: hoverSignIn.hovered ? Theme.accentInk : Theme.textMuted
                font.family: Theme.labelFont
                font.pixelSize: Theme.text2xs
                font.letterSpacing: 2
                HoverHandler { id: hoverSignIn }
                TapHandler { onTapped: root.signInRequested() }
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                size: "sm"
                icon: Theme.dark ? "sun" : "moon"
                onClicked: Theme.toggle()
            }
        }
    }

    // ---- Stage ----
    Item {
        id: content
        anchors { top: topbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }

        Item {
            id: band
            anchors.centerIn: parent
            width: Math.min(content.width - Theme.padStage * 2, root.wide ? 900 : 420)
            height: root.wide ? Math.max(hero.implicitHeight, card.implicitHeight)
                              : hero.implicitHeight + 32 + card.implicitHeight

            // Hero (left)
            ColumnLayout {
                id: hero
                width: root.wide ? band.width - card.width - 56 : band.width
                anchors.left: parent.left
                anchors.top: root.wide ? undefined : parent.top
                anchors.verticalCenter: root.wide ? parent.verticalCenter : undefined
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    textFormat: Text.RichText
                    text: root.heroTitle
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: root.wide ? 48 : 36
                    font.weight: Font.ExtraBold
                    font.letterSpacing: -1.5
                    lineHeight: 0.98
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 20
                    Layout.maximumWidth: 340
                    visible: text !== ""
                    text: root.heroSub
                    color: Theme.textMuted
                    font.family: Theme.uiFont
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }
                Row {
                    Layout.topMargin: 28
                    spacing: 10
                    Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: Theme.live }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.heroMeta.toUpperCase()
                        color: Theme.textFaint
                        font.family: Theme.labelFont
                        font.pixelSize: Theme.text2xs
                        font.letterSpacing: 2
                    }
                }
            }

            // Card (right)
            Card {
                id: card
                width: root.wide ? 380 : band.width
                anchors.right: parent.right
                anchors.top: root.wide ? undefined : hero.bottom
                anchors.topMargin: root.wide ? 0 : 32
                anchors.verticalCenter: root.wide ? parent.verticalCenter : undefined
                elevated: true
                spacing: 16
            }
        }
    }
}
