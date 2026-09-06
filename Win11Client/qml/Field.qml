import QtQuick
import MeetUp

// Labelled form row: uppercase micro-label · control · optional faint hint.
// Place a single control (e.g. AppInput) inside; bind its width to parent.
Column {
    id: root

    property string label: ""
    property string hint: ""
    // Настройка запланирована, но ещё не работает: к ярлыку добавляется пилюля
    // «скоро». Само поле при этом гасит и выключает вызывающая сторона —
    // Field не решает за неё (у «темы» половина вариантов рабочая).
    property bool soon: false
    // Настройка только для режима разработчика. Без него поля НЕТ — не серого,
    // не приглушённого, а отсутствующего: Column пропускает невидимого ребёнка
    // вместе с его отступом, и соседи смыкаются так, будто всегда были рядом.
    property bool dev: false
    default property alias content: holder.data

    visible: !root.dev || AV.devMode
    spacing: 6

    Row {
        visible: root.label !== ""
        spacing: 8
        Text {
            text: root.label.toUpperCase()
            color: Theme.textMuted
            font.family: Theme.labelFont
            font.pixelSize: Theme.text2xs
            font.letterSpacing: 2
            font.weight: Font.Medium
        }
        SoonChip {
            visible: root.soon
            anchors.verticalCenter: parent.verticalCenter
        }
        // Видно только тому, кто сам включил режим: пилюля отвечает на вопрос
        // «почему это вижу я и не видят остальные».
        SoonChip {
            visible: root.dev
            text: "dev"
            anchors.verticalCenter: parent.verticalCenter
        }
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
