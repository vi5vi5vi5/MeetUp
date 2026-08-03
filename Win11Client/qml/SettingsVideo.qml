import QtQuick
import QtMultimedia
import MeetUp

// Раздел «Видео»: камера и то, в каком виде она уходит собеседникам.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    // См. SettingsShare: проба стоит создания пробного кодировщика на каждый
    // аппаратный пункт, поэтому спрашиваем при открытии раздела и один раз.
    Component.onCompleted: Media.probeCodecs()

    readonly property var codecModel: {
        var m = [{ id: "auto", label: "Авто — подобрать самому" }]
        var opts = Media.codecOptions
        for (var i = 0; i < opts.length; ++i)
            m.push({ id: opts[i].id, label: opts[i].label })
        return m
    }

    Field {
        width: parent.width
        label: "Камера"
        SettingsCombo {
            width: parent.width
            model: AV.camDevices
            selectedId: AV.camId
            onPicked: function (id) { AV.camId = id }
        }
    }

    // Предпросмотр. Пока этот раздел открыт, камера снимает даже при выключенном
    // тумблере — иначе выбирать устройство пришлось бы вслепую, показывая пробы
    // собеседникам. Кадр остаётся на этом компьютере: движок ничего не кодирует
    // и не отправляет, пока камера не включена по-настоящему.
    Field {
        width: parent.width
        label: "Предпросмотр"
        hint: "Камера снимает, пока открыт этот раздел. Собеседники видят вас, только когда камера включена в конференции."
        Rectangle {
            id: box
            // Не во всю ширину: 16:9 от 520 px — это треть раздела, и «качество
            // отправки» уезжало бы под сгиб. Для проверки кадра хватает и такого.
            width: Math.min(parent.width, 360)
            height: width * 9 / 16
            radius: Theme.radiusMd
            border.width: 1
            border.color: Theme.border
            clip: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.tileFrom }
                GradientStop { position: 1.0; color: Theme.tileTo }
            }

            property int _tok: 0

            VideoOutput {
                id: out
                anchors.fill: parent
                anchors.margins: 1
                visible: Media.previewActive
                fillMode: VideoOutput.PreserveAspectCrop
                // Зеркало — как в своей плитке и как у веба: человек привык
                // видеть себя в зеркале, неотзеркаленное лицо читается как чужое.
                transform: Scale {
                    origin.x: out.width / 2
                    xScale: -1
                }
            }

            // Пока первый кадр не пришёл (камера просыпается ~полсекунды).
            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: !Media.previewActive
                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "video"
                    size: 26
                    color: Theme.textFaint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Камера включается…"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.textXs
                }
            }

            Component.onCompleted: box._tok = Media.attachPreviewExtra(out.videoSink)
            Component.onDestruction: Media.detachPreviewExtra(box._tok)
        }
    }

    Field {
        width: parent.width
        label: "Качество отправки"
        hint: "Выше качество — больше нагрузка на сеть. Применяется сразу."
        SettingsCombo {
            width: parent.width
            model: [ { id: "low",  label: "360p · эко" },
                     { id: "med",  label: "720p" },
                     { id: "high", label: "1080p" } ]
            selectedId: AV.camQuality
            onPicked: function (id) { AV.camQuality = id }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Кодек камеры. Список тот же, что у демонстрации, включая аппаратные
    // варианты, — но с оговоркой, которую человеку надо сказать прямо: у
    // кодировщика видеокарты конвейер глубиной два кадра, и на 24 к/с это
    // лишние ~83 мс прямо в расхождение картинки с голосом. Экрану такая плата
    // безразлична, лицу — нет, поэтому «Авто» держится процессора. Запрещать
    // выбор мы всё же не станем: на 60 к/с те же два кадра стоят втрое меньше,
    // а машины бывают очень разные.
    Field {
        width: parent.width
        label: "Кодек камеры"
        hint: AV.camCodec === "auto"
            ? "Сначала VP9: он кодируется вчетверо быстрее остальных программных, а значит меньше задержка. Если участник его не поймёт, перейдём на H.264, в крайнем случае на VP8."
            : "Выбранный кодек пробуется первым; не откроется или не поймут — включится обычный подбор. Аппаратные кодировщики держат внутри два кадра: на камере эта задержка заметна по губам, поэтому «Авто» их не берёт."
        SettingsCombo {
            width: parent.width
            model: page.codecModel
            selectedId: AV.camCodec
            onPicked: function (id) { AV.camCodec = id }
        }
    }

    SettingSwitch {
        label: "Зеркалить моё видео"
        description: "Только у вас. Собеседники видят кадр как есть."
        checked: true
        soon: true
    }
}
