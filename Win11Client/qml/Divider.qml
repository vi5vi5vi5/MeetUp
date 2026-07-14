import QtQuick
import QtQuick.Layouts
import MeetUp

// Horizontal rule with a centered uppercase label: ──── ИЛИ ────
RowLayout {
    property string label: "или"
    spacing: 12
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
    Text {
        text: label.toUpperCase()
        color: Theme.textFaint
        font.family: Theme.labelFont
        font.pixelSize: Theme.text2xs
        font.letterSpacing: 2
    }
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
}
