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

    Field {
        width: parent.width
        label: "Чувствительность микрофона · " + AV.sensitivity + "%"
        hint: "Влияет на громкость вашего голоса у собеседников."
        PercentSlider {
            width: parent.width
            value: AV.sensitivity
            onMoved: AV.sensitivity = Math.round(value)
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- Динамики ----
    Field {
        width: parent.width
        label: "Динамики"
        hint: "Проверка динамиков — скоро."
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
                text: "Проверить"
                variant: "ghost"
                size: "sm"
                enabled: false
                opacity: 0.5
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
    // Шумоподавление работает (RNNoise, см. src/media/Denoiser.h). Двум
    // оставшимся тумблерам нужен эхоподавитель WebRTC (AEC3): своя задача,
    // не «шумодав получше» — там нужен второй, опорный сигнал того, что ушло
    // в динамики, и оценка задержки тракта.
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
            SettingSwitch {
                label: "Эхоподавление"
                description: "Нужно, когда вы слушаете через колонки, а не наушники."
                soon: true
                checked: true
            }
            SettingSwitch {
                label: "Автоусиление"
                description: "Выравнивает громкость, если вы отсели от микрофона."
                soon: true
            }
        }
    }
}
