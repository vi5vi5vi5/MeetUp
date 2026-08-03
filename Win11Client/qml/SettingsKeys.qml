import QtQuick
import MeetUp

// Раздел «Управление». Клавиши здесь глобальные, а не оконные: смысл как раз
// в том, чтобы выключить микрофон, не возвращаясь в окно из демонстрируемой
// программы (механизм — в src/GlobalHotkeys.h).
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Field {
        width: parent.width
        hint: "Работают всегда, даже когда окно не в фокусе, и клавишу у системы не отнимают — назначенная буква по-прежнему печатается везде. Действие можно повесить и на один модификатор: правый Ctrl, правый Shift, правый Alt. Такой бинд срабатывает на отпускании и только если больше ничего не нажимали, поэтому правый Ctrl с буквой остаётся обычным сочетанием. Нажмите поле и введите клавишу: Backspace — очистить, Esc — отмена."
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

    // Рация ждёт уже только проводки: сырой ввод сообщает и о нажатии, и об
    // отпускании (см. GlobalHotkeys::onKey) — нужен свой сигнал на удержание
    // и обработка на стороне ConferenceScreen.
    SettingSwitch {
        label: "Рация"
        description: "Микрофон открыт, пока клавиша зажата."
        soon: true
    }
}
