import QtQuick
import MeetUp

// Раздел «О программе». Не про красоту: при разборе жалобы первым делом
// спрашивают версию и адрес сервера — пусть они будут под рукой у человека,
// а не в переписке.
Column {
    id: page
    width: parent ? parent.width : 0
    spacing: 18

    component InfoRow: Item {
        id: line
        property string key: ""
        property string value: ""
        property bool soon: false

        width: parent ? parent.width : 0
        height: 28

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: line.key
            color: Theme.textMuted
            font.family: Theme.uiFont
            font.pixelSize: Theme.textSm
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            SoonChip {
                visible: line.soon
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: line.value
                color: Theme.text
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
            }
        }
    }

    // Шапка раздела: знак, название, версия. Без неё раздел начинался прямо со
    // строки таблицы и читался как её продолжение, а не как «о программе».
    Row {
        width: parent.width
        spacing: 16

        BrandMark {
            anchors.verticalCenter: parent.verticalCenter
            size: 56
            plate: true
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: "MeetUp"
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: 26
                font.weight: Font.ExtraBold
                font.letterSpacing: -0.9
            }
            Text {
                text: "Версия " + Qt.application.version + " · Windows"
                color: Theme.textMuted
                font.family: Theme.uiFont
                font.pixelSize: Theme.textSm
            }
        }
    }

    Column {
        width: parent.width
        spacing: 2

        // «Версия клиента» отдельной строкой больше не нужна — она в шапке выше.
        InfoRow { key: "Версия сервера"; value: "—"; soon: true }
        InfoRow { key: "Сервер"; value: Sys.host }
        InfoRow { key: "Кодеки"; value: "openh264 · libvpx · opus" }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    Field {
        width: parent.width
        label: "Обслуживание"
        soon: true
        hint: "Журнал пока уходит в stderr — файла, который можно открыть, ещё нет."
        Row {
            width: parent.width
            spacing: 8
            enabled: false
            opacity: 0.62

            AppButton {
                width: (parent.width - 8) / 2
                text: "Папка с журналами"
                variant: "ghost"
                size: "sm"
            }
            AppButton {
                width: (parent.width - 8) / 2
                text: "Проверить обновления"
                variant: "ghost"
                size: "sm"
            }
        }
    }
}
