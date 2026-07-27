import QtQuick
import MeetUp

// Раздел «Управление». Клавиши здесь системные (RegisterHotKey), а не
// оконные: смысл как раз в том, чтобы выключить микрофон, не возвращаясь
// в окно из демонстрируемой программы.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Field {
        width: parent.width
        hint: "Работают всегда, даже когда окно не в фокусе. Можно назначить и одну клавишу — выбирайте ту, что не используете. Нажмите поле и введите сочетание: Backspace — очистить, Esc — отмена."
    }

    Column {
        width: parent.width
        spacing: 8

        KeyBind {
            label: "Микрофон"
            value: AV.keyMic
            onPicked: function (seq) { AV.keyMic = seq }
        }
        KeyBind {
            label: "Общий звук"
            value: AV.keySound
            onPicked: function (seq) { AV.keySound = seq }
        }
        KeyBind {
            label: "Камера"
            value: AV.keyCam
            onPicked: function (seq) { AV.keyCam = seq }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Механизм тот же — не хватает только идентификаторов в GlobalHotkeys
    // и действий на стороне ConferenceScreen.
    Field {
        width: parent.width
        label: "Ещё действия"
        soon: true
        Column {
            width: parent.width
            spacing: 8
            enabled: false
            opacity: 0.62

            KeyBind { label: "Демонстрация экрана" }
            KeyBind { label: "Во весь экран" }
            KeyBind { label: "Завершить звонок" }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Рация ждёт не интерфейса, а механизма: WM_HOTKEY сообщает только о
    // нажатии, отпускание придётся ловить иначе.
    SettingSwitch {
        label: "Рация"
        description: "Микрофон открыт, пока клавиша зажата."
        soon: true
    }
}
