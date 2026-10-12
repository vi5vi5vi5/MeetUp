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

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // «Картинка в картинке» руками выносится кнопкой на плитке или на
    // демонстрации; здесь — только то, что окно умеет делать само.
    SettingSwitch {
        width: parent.width
        label: "Выносить демонстрацию при сворачивании"
        description: "Свернули MeetUp, пока кто-то показывает экран, — показ останется "
                   + "поверх всех окон в маленьком окне. Развернули — вернулся на место."
        checked: AV.autoPip
        onToggled: function (v) { AV.autoPip = v }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Предпросмотр делает сервер (см. LinkPreviews.h); здесь — только
    // согласие человека. Сервер, который этого не умеет, тумблер не включит.
    Column {
        width: parent.width
        spacing: 14

        SettingSwitch {
            label: "Предпросмотр ссылок в чате"
            description: Server.linkPreviews
                ? "Под сообщением со ссылкой — заголовок, описание и картинка страницы. "
                  + "Страницу открывает сервер MeetUp, а не ваш компьютер: сайт не узнает ваш адрес."
                : "Этот сервер предпросмотр не делает — ссылки в чате остаются ссылками."
            checked: AV.linkPreviews && Server.linkPreviews
            enabled: Server.linkPreviews
            onToggled: function (v) { AV.linkPreviews = v }
        }
        SettingSwitch {
            label: "…и в зашифрованных комнатах"
            description: "Чтобы показать карточку, сервер должен узнать адрес ссылки, а в комнате "
                       + "со сквозным шифрованием переписку он иначе не видит. Поэтому там по "
                       + "умолчанию выключено. Ключи из ссылок (всё после «#») не уходят никогда."
            checked: AV.linkPreviewsE2E
            enabled: Server.linkPreviews && AV.linkPreviews
            onToggled: function (v) { AV.linkPreviewsE2E = v }
        }
    }
}
