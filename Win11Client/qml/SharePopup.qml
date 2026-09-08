import QtQuick
import MeetUp

// Поповер «поделиться ссылкой» — один в один .share-pop у веба. Открывается
// кнопкой копирования в шапке конференции и сразу кладёт обычную ссылку в
// буфер (это делает ConferenceScreen::openShare, как openShare у веба); сама
// карточка показывает адрес, кнопку «Копировать» и, если шифрование включено,
// вторую — «Ссылка с ключом E2E». Ключ едет во фрагменте адреса (#k=…), эта
// часть на сервер не отправляется.
//
// Карточка висит под шапкой у правого края сцены, а не по центру: её открывают
// у той же кнопки, что нажали, и закрывают через секунду. Скрим прозрачный —
// тут нечего подчёркивать, он ловит щелчок мимо и не пускает его к плиткам.
// Сама карточка лежит на поглотителе-MouseArea, а не ловит щелчки хендлером:
// только элемент останавливает нажатие по-настоящему (см. AppModal).
Item {
    id: root

    property bool open: false
    property string link: ""          // обычная ссылка-приглашение
    property string keyLink: ""       // ссылка с ключом; "" — шифрования нет
    property bool copied: false       // «Скопировано» у обычной ссылки
    property bool keyCopied: false    // …и у ссылки с ключом
    // Правый край карточки и её отступ сверху — в координатах этого слоя.
    property real anchorRight: width
    property real anchorTop: 72

    // Копирование делает владелец: у него флаги подсветки и таймеры.
    signal copyRequested()
    signal copyKeyRequested()

    anchors.fill: parent
    visible: open
    z: 150      // ниже настроек (200), выше плиток и дока

    // Esc закрывает, как у веба. Фокус берём только пока открыты — иначе
    // отняли бы клавиши у чата.
    focus: open
    Keys.onEscapePressed: root.open = false

    MouseArea {
        anchors.fill: parent
        onClicked: root.open = false
    }

    Item {
        id: box
        width: Math.min(360, root.width - 32)
        x: Math.max(16, root.anchorRight - width)
        y: root.anchorTop
        height: card.implicitHeight

        // Поглотитель под карточкой: щелчки по ней до скрима не доходят.
        MouseArea { anchors.fill: parent }

        Card {
            id: card
            elevated: true
            width: parent.width
            spacing: 12

            Text {
                text: "ССЫЛКА-ПРИГЛАШЕНИЕ"
                color: Theme.textMuted
                font.family: Theme.labelFont
                font.pixelSize: Theme.text2xs
                font.weight: Font.Bold
                font.letterSpacing: 2
            }

            // Только чтение, но с выделением: адрес удобно выделить и глазами
            // сверить, а щелчок в поле выделяет его целиком — как в вебе.
            AppInput {
                id: field
                width: parent.width
                readOnly: true
                text: root.link
                // Адрес длиннее поля. TextField после присваивания ставит курсор
                // в конец и показывает хвост «…?room=…»; человеку нужнее начало —
                // домен, по которому видно, чей это сервер. Как у веба.
                onTextChanged: cursorPosition = 0
                Component.onCompleted: cursorPosition = 0
                onActiveFocusChanged: if (activeFocus) selectAll()
            }

            AppButton {
                width: parent.width
                variant: "primary"
                icon: root.copied ? "check" : "copy"
                text: root.copied ? "Скопировано" : "Копировать"
                onClicked: root.copyRequested()
            }

            AppButton {
                visible: root.keyLink !== ""
                width: parent.width
                variant: "secondary"
                icon: root.keyCopied ? "check" : "copy"
                text: root.keyCopied ? "Скопировано" : "Ссылка с ключом E2E"
                onClicked: root.copyKeyRequested()
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.keyLink !== ""
                    ? "Обычная ссылка не содержит ключ: вошедший по ней увидит «зашифровано»."
                    : "По ссылке входят без аккаунта: имя — и сразу в эфир."
                color: Theme.textFaint
                font.family: Theme.uiFont
                font.pixelSize: Theme.textXs
            }
        }
    }
}
