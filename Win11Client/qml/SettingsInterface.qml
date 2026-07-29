import QtQuick
import MeetUp

// Раздел «Интерфейс»: то, что меняет вид окна, а не поток данных.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Field {
        width: parent.width
        label: "Тема"
        hint: "Кнопка солнце/луна в шапке остаётся — это просто её постоянный дом."
        SegmentedControl {
            width: parent.width
            current: Theme.dark ? "dark" : "light"
            model: [ { id: "dark",   label: "Тёмная" },
                     { id: "light",  label: "Светлая" },
                     { id: "system", label: "Как в системе", soon: true } ]
            onPicked: function (id) { Theme.dark = (id === "dark") }
        }
    }

    Field {
        width: parent.width
        label: "Плиток на странице"
        soon: true
        hint: "Сейчас девять зашито в код; на большом мониторе просятся двенадцать."
        SegmentedControl {
            width: parent.width
            enabled: false
            opacity: 0.62
            current: "9"
            model: [ { id: "6", label: "6" }, { id: "9", label: "9" }, { id: "12", label: "12" } ]
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    SettingSwitch {
        width: parent.width
        label: "Звуки интерфейса"
        description: "Микрофон и камера, сообщение в чате, вход и выход участников. "
                   + "Звучат в те же наушники, что и разговор."
        checked: AV.uiSounds
        onToggled: function (v) { AV.uiSounds = v }
    }

    Column {
        width: parent.width
        spacing: 14
        enabled: false
        opacity: 0.62

        SettingSwitch {
            label: "Показывать себя в сетке"
            description: "Выключите — своя плитка уедет в угол и освободит место."
            checked: true
        }
        SettingSwitch {
            label: "Скрывать участников без видео"
            description: "В больших комнатах на экране остаются только говорящие."
        }
    }
}
