import QtQuick
import MeetUp

// Labelled form row: uppercase micro-label · control · optional faint hint.
// Place a single control (e.g. AppInput) inside; bind its width to parent.
Column {
    id: root

    property string label: ""
    property string hint: ""
    default property alias content: holder.data

    spacing: 6

    Text {
        visible: root.label !== ""
        text: root.label.toUpperCase()
        color: Theme.textMuted
        font.family: Theme.labelFont
        font.pixelSize: Theme.text2xs
        font.letterSpacing: 2
        font.weight: Font.Medium
    }

    Item {
        id: holder
        width: root.width
        implicitHeight: childrenRect.height
    }

    Text {
        visible: root.hint !== ""
        text: root.hint
        width: root.width
        wrapMode: Text.Wrap
        color: Theme.textFaint
        font.family: Theme.uiFont
        font.pixelSize: Theme.textXs
    }
}
