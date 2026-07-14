import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Single-line text input. surface-2 fill, hairline border that turns accent
// on focus. Set isPassword for masked entry.
TextField {
    id: control

    property bool isPassword: false

    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.family: Theme.uiFont
    font.pixelSize: Theme.textMd
    font.weight: Font.Medium
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentFg
    echoMode: isPassword ? TextInput.Password : TextInput.Normal

    leftPadding: 14
    rightPadding: 14
    topPadding: 12
    bottomPadding: 12

    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.surface2
        border.width: 1
        border.color: control.activeFocus ? Theme.accentInk : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }
}
