import QtQuick
import QtQuick.Controls.Basic
import MeetUp

// Настройки конференции (модалка поверх сцены): выбор устройств ввода-вывода,
// громкость воспроизведения, чувствительность микрофона, качество отправки.
// Всё пишется в AV (MediaSettings) и применяется движками на лету; значения
// переживают перезапуск. Повторяет модалку настроек web-клиента.
AppModal {
    id: root
    title: "Настройки"
    modalWidth: 440

    // ---- Тематизированный выпадающий список ----
    component SettingsCombo: ComboBox {
        id: combo

        property string selectedId: ""
        signal picked(string id)

        textRole: "label"
        valueRole: "id"
        height: 40

        onActivated: picked(currentValue)

        // Индекс следует за выбранным id — и при живом обновлении списка
        // устройств (подключили гарнитуру), и при первом открытии.
        function sync() {
            var i = indexOfValue(selectedId)
            currentIndex = i >= 0 ? i : 0
        }
        onSelectedIdChanged: sync()
        onModelChanged: sync()
        Component.onCompleted: sync()

        contentItem: Text {
            leftPadding: 12
            rightPadding: 30
            verticalAlignment: Text.AlignVCenter
            text: combo.displayText
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        indicator: Text {
            x: combo.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Theme.textMuted
            font.pixelSize: Theme.textSm
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.surface2
            border.width: 1
            border.color: combo.popup.visible ? Theme.accent : Theme.border
        }
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            width: combo.width - 12
            height: 34
            contentItem: Text {
                verticalAlignment: Text.AlignVCenter
                text: row.modelData.label
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
            background: Rectangle {
                radius: Theme.radiusXs
                color: row.highlighted ? Theme.surface3 : "transparent"
            }
            highlighted: combo.highlightedIndex === index
        }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            padding: 6
            background: Rectangle {
                radius: Theme.radiusSm
                color: Theme.surface
                border.width: 1
                border.color: Theme.borderStrong
            }
            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, 240)
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
            }
        }
    }

    // ---- Слайдер процентов (0..200, как у веба) ----
    component PercentSlider: Slider {
        id: slider
        from: 0
        to: 200
        stepSize: 5
        height: 26

        background: Rectangle {
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: slider.width
            height: 6
            radius: 3
            color: Theme.surface3
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 3
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.visualPosition * (slider.width - width)
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: Theme.text
            border.width: 1
            border.color: Theme.border
        }
    }

    // ---- Захват горячей клавиши ----
    // Клик по полю переводит его в «слушаю»: следующая нажатая комбинация
    // становится биндом. Backspace/Delete очищают, Esc отменяет. Пока слушаем,
    // onShortcutOverride перехватывает даже клавиши, которые иначе сработали бы
    // как глобальные сочетания (например F11), чтобы записать именно их.
    component KeyBind: Item {
        id: kb
        property string label: ""
        property string value: ""
        signal picked(string seq)

        width: parent.width
        height: 38
        property bool listening: false

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - box.width - 12
            elide: Text.ElideRight
            text: kb.label
            color: Theme.text
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
            font.weight: Font.Medium
        }

        Rectangle {
            id: box
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 168
            height: 34
            radius: Theme.radiusSm
            color: Theme.surface2
            border.width: kb.listening ? 2 : 1
            border.color: kb.listening ? Theme.accent : Theme.border

            Text {
                anchors.centerIn: parent
                width: parent.width - 20
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: kb.listening ? "Нажмите клавишу…"
                    : (kb.value !== "" ? kb.value : "Не задано")
                color: kb.listening ? Theme.accentInk
                    : (kb.value !== "" ? Theme.text : Theme.textFaint)
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.Medium
            }

            Keys.onShortcutOverride: function (event) { event.accepted = kb.listening }
            Keys.onPressed: function (event) {
                if (!kb.listening) return
                event.accepted = true
                if (event.key === Qt.Key_Escape) { kb.listening = false; return }
                if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                    kb.picked(""); kb.listening = false; return
                }
                var seq = AV.sequenceFromEvent(event.key, event.modifiers)
                if (seq !== "") { kb.picked(seq); kb.listening = false }
                // одинокий модификатор/зарезервированная клавиша — ждём дальше
            }
            // Потерял фокус, не дослушав, — выходим из режима захвата.
            onActiveFocusChanged: if (!activeFocus) kb.listening = false

            TapHandler {
                onTapped: { kb.listening = true; box.forceActiveFocus() }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }

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

    // ---- Камера ----
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

    // ---- Динамики ----
    Field {
        width: parent.width
        label: "Динамики"
        SettingsCombo {
            width: parent.width
            model: AV.outDevices
            selectedId: AV.outId
            onPicked: function (id) { AV.outId = id }
        }
    }

    // ---- Громкость ----
    Field {
        width: parent.width
        label: "Громкость воспроизведения · " + AV.volume + "%"
        PercentSlider {
            width: parent.width
            value: AV.volume
            onMoved: AV.volume = Math.round(value)
        }
    }

    // ---- Чувствительность ----
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

    // ---- Качество отправки ----
    Field {
        width: parent.width
        label: "Качество отправки"
        hint: "Выше качество — больше нагрузка на сеть. Применяется сразу."
        Row {
            width: parent.width
            spacing: 8
            Column {
                width: (parent.width - 8) / 2
                spacing: 4
                Text {
                    text: "Камера"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
                SettingsCombo {
                    width: parent.width
                    model: [ { id: "low", label: "360p · эко" },
                             { id: "med", label: "720p" },
                             { id: "high", label: "1080p" } ]
                    selectedId: AV.camQuality
                    onPicked: function (id) { AV.camQuality = id }
                }
            }
            Column {
                width: (parent.width - 8) / 2
                spacing: 4
                Text {
                    text: "Звук"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
                SettingsCombo {
                    width: parent.width
                    model: [ { id: "low", label: "16 кбит/с" },
                             { id: "med", label: "32 кбит/с" },
                             { id: "high", label: "64 кбит/с" } ]
                    selectedId: AV.audioQuality
                    onPicked: function (id) { AV.audioQuality = id }
                }
            }
        }
    }

    // ---- Демонстрация экрана ----
    // Отдельно от камеры: у экрана свои требования — там читают текст, а не
    // смотрят на лицо, поэтому разрешение и частоту кадров выбирают порознь.
    Field {
        width: parent.width
        label: "Демонстрация экрана"
        hint: "«Источник» отдаёт кадр без масштабирования. Больше кадров — плавнее курсор и видео, выше нагрузка."
        Row {
            width: parent.width
            spacing: 8
            Column {
                width: (parent.width - 8) / 2
                spacing: 4
                Text {
                    text: "Разрешение"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
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
            Column {
                width: (parent.width - 8) / 2
                spacing: 4
                Text {
                    text: "Частота кадров"
                    color: Theme.textFaint
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.text2xs
                }
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
    }

    // ---- Горячие клавиши ----
    Field {
        width: parent.width
        label: "Горячие клавиши"
        hint: "Работают всегда, даже когда окно не в фокусе (удобно при демонстрации). Можно и одну клавишу — выбирайте ту, что не используете. Backspace — очистить, Esc — отмена."
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
    }
}
