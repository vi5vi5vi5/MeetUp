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

    // Механизм тот же, что у трёх верхних, — разница только в том, что эти три
    // действия имеют смысл лишь внутри конференции: вне её слушать их некому
    // (обработчики живут в ConferenceScreen и уходят вместе с ним).
    Field {
        width: parent.width
        label: "Ещё действия"
        hint: "«Во весь экран» разворачивает идущую демонстрацию, а если её нет — само окно."
        Column {
            width: parent.width
            spacing: 8

            KeyBind {
                label: "Демонстрация экрана"
                value: AV.keyShare
                onPicked: function (seq) { AV.keyShare = seq }
            }
            KeyBind {
                label: "Во весь экран"
                value: AV.keyFull
                onPicked: function (seq) { AV.keyFull = seq }
            }
            KeyBind {
                label: "Завершить звонок"
                value: AV.keyLeave
                onPicked: function (seq) { AV.keyLeave = seq }
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Рация переопределяет клавишу микрофона, а не заводит свою: две клавиши на
    // один микрофон — «выключатель» и «удержание» — спорили бы между собой, и
    // объяснить их разницу человеку было бы нечем.
    SettingSwitch {
        label: "Рация"
        description: AV.keyMic === ""
            ? "Сначала назначьте клавишу микрофона выше — рация работает ею."
            : "Микрофон открыт, пока зажата клавиша микрофона ("
              + Hotkeys.label(AV.keyMic) + "). Отпустили — закрылся."
        // Режим без органа управления включать нельзя: микрофон в нём не
        // открылся бы никогда, а тумблер стоял бы включённым и врал.
        enabled: AV.keyMic !== ""
        checked: AV.pushToTalk
        onToggled: function (v) { AV.pushToTalk = v }
    }
}
