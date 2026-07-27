import QtQuick
import MeetUp

// Раздел «Демонстрация». Отдельно от камеры не для порядка, а по существу:
// на экране читают текст, а не смотрят на лицо, поэтому разрешение и частоту
// кадров выбирают порознь, а кодеки ведут себя иначе, чем на видео с камеры.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    Row {
        width: parent.width
        spacing: 8

        Field {
            width: (parent.width - 8) / 2
            label: "Разрешение"
            SettingsCombo {
                width: parent.width
                model: [ { id: "360",  label: "360p" },
                         { id: "480",  label: "480p" },
                         { id: "720",  label: "720p" },
                         { id: "1080", label: "1080p" },
                         { id: "src",  label: "Источник" } ]
                selectedId: AV.screenRes
                onPicked: function (id) { AV.screenRes = id }
            }
        }
        Field {
            width: (parent.width - 8) / 2
            label: "Частота кадров"
            SettingsCombo {
                width: parent.width
                // valueRole у комбо строковый — число приводим на месте.
                model: [ { id: "15", label: "15 к/с" },
                         { id: "30", label: "30 к/с" },
                         { id: "60", label: "60 к/с" } ]
                selectedId: String(AV.screenFps)
                onPicked: function (id) { AV.screenFps = parseInt(id) }
            }
        }
    }

    Field {
        width: parent.width
        hint: "«Источник» отдаёт кадр без масштабирования. Больше кадров — плавнее курсор и видео, выше нагрузка."
    }

    Field {
        width: parent.width
        label: "Кодек демонстрации"
        hint: "Отдельно от камеры: на тексте и графиках кодеки ведут себя не так, как на лицах. VP9 обычно чище рисует мелкий шрифт, но и грузит процессор сильнее."
        SegmentedControl {
            width: parent.width
            current: AV.screenCodec
            model: [ { id: "auto", label: "Авто" },
                     { id: "h264", label: "H.264" },
                     { id: "vp8",  label: "VP8" },
                     { id: "vp9",  label: "VP9" },
                     { id: "av1",  label: "AV1", soon: true } ]
            onPicked: function (id) { AV.screenCodec = id }
        }
    }

    SettingSwitch {
        label: "Показывать курсор"
        description: "Мы дорисовываем стрелку сами — захват монитора её не отдаёт. При показе отдельного окна курсор рисует система, и настройка на него не влияет."
        checked: AV.screenCursor
        onToggled: function (v) { AV.screenCursor = v }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // Звук демонстрации идёт отдельной полосой (тип кадра 8), и захватывается
    // он в режиме «всё, кроме нас самих» — поэтому голоса собеседников из
    // наших динамиков в него не попадают и эха у них не будет.
    SettingSwitch {
        label: "Передавать звук компьютера"
        description: "Видео, музыка, звук игры — то, что слышите вы. Ваши собеседники в этот звук не попадают."
        checked: AV.screenAudio
        onToggled: function (v) { AV.screenAudio = v }
    }

    Item {
        width: parent.width
        height: nested.implicitHeight
        enabled: AV.screenAudio
        opacity: AV.screenAudio ? 1 : 0.62

        // Черта слева связывает подпункт с его выключателем — на приглушённом
        // блоке обычный border почти не виден, поэтому borderStrong.
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: Theme.borderStrong
        }

        Column {
            id: nested
            anchors.left: parent.left
            anchors.leftMargin: 17
            anchors.right: parent.right
            spacing: 14

            Field {
                width: parent.width
                label: "Громкость звука демонстрации · " + AV.screenAudioGain + "%"
                hint: "Тише голоса — чтобы фонограмма не заглушала разговор."
                PercentSlider {
                    width: parent.width
                    value: AV.screenAudioGain
                    onMoved: AV.screenAudioGain = Math.round(value)
                }
            }
        }
    }
}
