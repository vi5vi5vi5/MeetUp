import QtQuick
import MeetUp

// Раздел «Звук»: всё про голос — свой и чужой. Порядок не случаен: сначала то,
// что слышат собеседники (микрофон и его чувствительность), потом то, что
// слышите вы (динамики и громкость).
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    // ---- Микрофон ----
    Field {
        width: parent.width
        label: "Микрофон"
        Column {
            width: parent.width
            spacing: 8
            SettingsCombo {
                width: parent.width
                model: AV.micDevices
                selectedId: AV.micId
                onPicked: function (id) { AV.micId = id }
            }
            // Живой уровень: говорите — полоска дышит (значит, устройство то).
            Rectangle {
                width: parent.width
                height: 6
                radius: 3
                color: Theme.surface3
                Rectangle {
                    width: parent.width * Math.min(1, AV.micLevel * 3)
                    height: parent.height
                    radius: 3
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 80 } }
                }
            }
        }
    }

    // При включённом автоусилении ползунок меняет роль: он больше не орган
    // управления, а индикатор — показывает множитель, который сейчас держит
    // AutoGain. Шкала остаётся прежней (0..200 %), поэтому на больших усилениях
    // ручка упирается в правый край; честное число стоит рядом, в подписи.
    Field {
        width: parent.width
        label: "Чувствительность микрофона · "
               + (AV.autoGain ? AV.agcSensitivity : AV.sensitivity) + "%"
        hint: AV.autoGain
              ? "Подбирается автоматически. Выключите автоусиление, чтобы задать вручную."
              : "Влияет на громкость вашего голоса у собеседников."
        PercentSlider {
            width: parent.width
            enabled: !AV.autoGain
            value: AV.autoGain ? AV.agcSensitivity : AV.sensitivity
            onMoved: AV.sensitivity = Math.round(value)
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- Динамики ----
    Field {
        id: spkField
        width: parent.width
        label: "Динамики"
        hint: "«Проверить» сыграет короткий сигнал в выбранное устройство — так узнают, те ли это наушники. Голоса собеседников пойдут туда же."
        // Кнопка на секунду меняет подпись: сигнал короткий, и без этого
        // нажатие в тишине (не то устройство) выглядело бы как «не сработало».
        property bool _testing: false
        Timer { id: testReset; interval: 1200; onTriggered: spkField._testing = false }
        Item {
            width: parent.width
            height: 40
            SettingsCombo {
                anchors.left: parent.left
                anchors.right: testBtn.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                model: AV.outDevices
                selectedId: AV.outId
                onPicked: function (id) { AV.outId = id }
            }
            AppButton {
                id: testBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: spkField._testing ? "Играет…" : "Проверить"
                variant: spkField._testing ? "primary" : "ghost"
                size: "sm"
                onClicked: { Sfx.test(); spkField._testing = true; testReset.restart() }
            }
        }
    }

    Field {
        width: parent.width
        label: "Громкость воспроизведения · " + AV.volume + "%"
        PercentSlider {
            width: parent.width
            value: AV.volume
            onMoved: AV.volume = Math.round(value)
        }
    }

    Field {
        width: parent.width
        label: "Качество звука"
        SettingsCombo {
            width: parent.width
            model: [ { id: "low",  label: "16 кбит/с — экономно" },
                     { id: "med",  label: "32 кбит/с — обычно" },
                     { id: "high", label: "64 кбит/с — музыкально" } ]
            selectedId: AV.audioQuality
            onPicked: function (id) { AV.audioQuality = id }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- Обработка голоса ----
    // Все три работают: шумоподавление (RNNoise, src/media/Denoiser.h),
    // эхоподавление (SpeexDSP, src/media/EchoCanceller.h) и автоусиление
    // (src/media/AutoGain.h). В тракте стоят в этом же порядке — эхо, шум,
    // усиление: каждому следующему нужен результат предыдущего.
    Field {
        width: parent.width
        label: "Обработка голоса"
        Column {
            width: parent.width
            spacing: 14

            SettingSwitch {
                label: "Шумоподавление"
                description: "Убирает вентилятор, клавиатуру и фон комнаты."
                checked: AV.noiseSuppression
                onToggled: function (v) { AV.noiseSuppression = v }
            }
            // Вычитает из микрофона то, что сами сыграли в динамики. Насколько
            // это удаётся, видно в «Диагностике» (подавление и найденная
            // задержка эха).
            SettingSwitch {
                label: "Эхоподавление"
                description: "Нужно, когда вы слушаете через колонки, а не наушники: убирает из микрофона голоса собеседников."
                checked: AV.echoCancel
                onToggled: function (v) { AV.echoCancel = v }
            }
            // Требует шумоподавления, и это не прихоть: без него усиливать
            // пришлось бы комнату вместе с голосом, поэтому автоусиление там
            // упирается в собственный предохранитель и почти ничего не делает.
            // Тумблер, который включается и не работает, хуже недоступного.
            SettingSwitch {
                label: "Автоусиление"
                description: AV.noiseSuppression
                    ? "Выравнивает громкость, если вы отсели от микрофона."
                    : "Нужно шумоподавление: иначе фон вытягивался бы вместе с голосом."
                enabled: AV.noiseSuppression
                checked: AV.autoGain
                onToggled: function (v) { AV.autoGain = v }
            }
        }
    }
}
