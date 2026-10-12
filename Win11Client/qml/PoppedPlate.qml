import QtQuick
import MeetUp

// Заглушка на месте видео, вынесенного в отдельное окно («картинка в
// картинке»): на плитке, на закреплённой сцене и на сцене демонстрации.
//
// Пустое место на месте человека читается как «отвалился», а замерший
// последний кадр — как «завис». Поэтому здесь прямо сказано, где видео, и
// рядом — способ вернуть его одним нажатием.
Item {
    id: root

    property string title: "Видео в отдельном окне"
    // Пояснение под заголовком — только на крупной сцене, где ему есть место.
    property string hint: ""
    // Плёнка (168×96): только значок-кнопка, подписи там не поместятся.
    readonly property bool compact: height < 130 || width < 200
    signal returnRequested()

    Column {
        anchors.centerIn: parent
        // На плёнке чип с именем занимает низ плитки — поднимаемся над ним.
        anchors.verticalCenterOffset: root.compact ? -10 : 0
        width: Math.min(parent.width - 32, 340)
        spacing: root.compact ? 0 : 10

        // Значок в кружке — он же кнопка возврата на маленькой плитке.
        Rectangle {
            id: badge
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.compact ? 34 : 46
            height: width
            radius: width / 2
            color: badgeHover.hovered && root.compact ? Theme.accentLine : Theme.accentSoft
            border.width: 1
            border.color: Theme.accentLine
            Behavior on color { ColorAnimation { duration: Theme.durFast } }

            AppIcon {
                anchors.centerIn: parent
                name: root.compact ? "pip-return" : "pip"
                size: root.compact ? 16 : 20
                color: Theme.accentInk
            }
            HoverHandler {
                id: badgeHover
                enabled: root.compact
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                enabled: root.compact
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: root.returnRequested()
            }
        }

        Text {
            visible: !root.compact
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.title
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.DemiBold
        }
        Text {
            visible: !root.compact && root.hint !== "" && root.height >= 260
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.hint
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textXs
        }
        Item { visible: !root.compact; width: 1; height: 2 }
        AppButton {
            visible: !root.compact
            anchors.horizontalCenter: parent.horizontalCenter
            size: "sm"
            variant: "secondary"
            icon: "pip-return"
            text: "Вернуть сюда"
            onClicked: root.returnRequested()
        }
    }
}
