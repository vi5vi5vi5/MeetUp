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
        hint: Theme.mode === "system"
            ? "Следует за настройкой Windows и меняется вместе с ней. Кнопка солнце/луна в шапке переключит на явную тему."
            : "Кнопка солнце/луна в шапке остаётся — это просто её постоянный дом. Выбор помнится между запусками."
        SegmentedControl {
            width: parent.width
            current: Theme.mode
            model: [ { id: "dark",   label: "Тёмная" },
                     { id: "light",  label: "Светлая" },
                     { id: "system", label: "Как в системе" } ]
            onPicked: function (id) { Theme.mode = id }
        }
    }

    // Сетка, а не окно: в режиме сцены плёнка сверху показывает всех подряд,
    // и страниц у неё нет.
    Field {
        width: parent.width
        label: "Плиток на странице"
        hint: "Больше плиток — мельче каждая. На большом мониторе двенадцать читаются, на ноутбуке лучше шесть. Остальные — на следующих страницах."
        SegmentedControl {
            width: parent.width
            current: String(AV.perPage)
            model: [ { id: "6", label: "6" }, { id: "9", label: "9" }, { id: "12", label: "12" } ]
            onPicked: function (id) { AV.perPage = parseInt(id) }
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

    // Оба фильтра — только про плитки (сетку и плёнку): список участников в
    // панели остаётся полным, там люди, а не картинки.
    Column {
        width: parent.width
        spacing: 14

        SettingSwitch {
            label: "Показывать себя в сетке"
            description: "Выключите — своя плитка исчезнет из сетки и освободит место. "
                       + "Как вы выглядите, всегда видно в предпросмотре камеры."
            checked: AV.showSelf
            onToggled: function (v) { AV.showSelf = v }
        }
        SettingSwitch {
            label: "Скрывать участников без видео"
            description: "В большой комнате на экране остаются только те, кто включил камеру. "
                       + "Выключенная камера — не молчание: голос слышен как обычно."
            checked: AV.hideNoVideo
            onToggled: function (v) { AV.hideNoVideo = v }
        }
    }
}
