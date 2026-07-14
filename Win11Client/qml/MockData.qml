pragma Singleton
import QtQuick

// Placeholder data for the M0 UI shell. Replaced by live server data in M1.
QtObject {
    property string serverAddress: "meetup.linkpc.net"
    property string roomTitle: "Комната Игоря"
    property string roomCode: "aX7Kp2Rf9Lm3Qz8Vn4Bt6Yw"
    property string selfName: "Вы"

    property var participants: [
        { name: "Игорь",           mic: true,  cam: true,  speaking: false, isSelf: true  },
        { name: "Анна Каренина",   mic: true,  cam: true,  speaking: true,  isSelf: false },
        { name: "Пётр Иванов",     mic: false, cam: false, speaking: false, isSelf: false },
        { name: "Мария",           mic: true,  cam: false, speaking: false, isSelf: false },
        { name: "Дмитрий Соколов", mic: true,  cam: true,  speaking: false, isSelf: false }
    ]

    property var messages: [
        { author: "Анна Каренина",   text: "Привет всем! Меня хорошо слышно?",              time: "14:02", self: false },
        { author: "Вы",              text: "Да, отлично 👍",                                 time: "14:02", self: true  },
        { author: "Пётр Иванов",     text: "Погнали, я готов начинать демонстрацию экрана.", time: "14:03", self: false },
        { author: "Вы",              text: "Секунду, поделюсь ссылкой на доску.",            time: "14:03", self: true  },
        { author: "Дмитрий Соколов", text: "Отличный дизайн у нового клиента, кстати.",      time: "14:04", self: false },
        { author: "Мария",           text: "Согласна, тёмная тема — огонь.",                 time: "14:05", self: false }
    ]

    // ---- home.html mock ----
    property string userName: "Игорь"

    // Личная комната владельца (объект room с сервера). online=true -> в эфире.
    property var personalRoom: ({
        code: "igor-room",
        title: "Комната Игоря",
        password: "kot123",
        online: true,
        participants: 3,
        participantNames: ["Анна Каренина", "Пётр Иванов", "Мария"],
        liveSinceLabel: "05:14"
    })

    // Недавние конференции (в вебе — localStorage браузера).
    property var history: [
        { code: "standup-daily", title: "Дейлик команды",   when: "12 мин назад" },
        { code: "aX7Kp2Rf9Lm3", title: "aX7Kp2Rf9Lm3",      when: "2 ч назад"    },
        { code: "design-review", title: "Ревью дизайна",     when: "вчера"        }
    ]

    // Alias-ссылки на личную комнату.
    property var aliases: [
        { id: 1, code: "team-a1b2", password: "",       usesLeft: -1, logins: ["anna", "ivan"], enabled: true  },
        { id: 2, code: "guest-x9y8", password: "kot123", usesLeft: 3,  logins: [],               enabled: false }
    ]
}
