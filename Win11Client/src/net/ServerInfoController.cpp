#include "ServerInfoController.h"
#include "ApiClient.h"

#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QNetworkReply>

ServerInfoController::ServerInfoController(ApiClient* api, QObject* parent)
    : QObject(parent), m_api(api) {
    refresh();
}

void ServerInfoController::reset() {
    // Умолчания РАЗРЕШАЮЩИЕ. Сервер, который не ответил, — это не сервер,
    // который всё запретил: старые сборки про /api/config не знают вовсе, и
    // прятать у них половину интерфейса было бы враньём про их настройки.
    m_loaded = false;
    m_name = QStringLiteral("MeetUp");
    m_registration = true;
    m_anonymousJoin = true;
    m_anonymousRooms = QStringLiteral("open");
    m_minPasswordLen = 8;
    m_codeMinLen = 3;
    m_maxAliases = 5;
    m_chatImageMaxKb = 440;
    m_maxPersonalRooms = 1;
    m_maxScreenShares = 1;
    m_logsNames = false;
    m_versionCommit.clear();
    m_versionModified = false;
    m_pingMs = -1;
    m_reachable = false;
}

void ServerInfoController::refresh() {
    // Часы заводим ДО запроса и читаем в обработчике: получится честный круг
    // «ушло — вернулось», включая TLS-рукопожатие на первом обращении.
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QNetworkReply* reply = m_api->get("/api/config");

    connect(reply, &QNetworkReply::finished, this, [this, reply, clock]() {
        reply->deleteLater();
        const int elapsed = int(clock->elapsed());
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();

        reset();
        m_pingMs = elapsed;
        m_reachable = status != 0;
        if (status != 200) {
            // 404 — сервер старее этой ручки, 0 — не достучались. И то и другое
            // означает «правил не знаем», а не «всё запрещено».
            emit changed();
            return;
        }

        // Каждое поле необязательно: сервер мог быть собран до того, как оно
        // появилось. Берём значение только если оно есть — иначе умолчание.
        const auto boolAt = [&o](const char* key, bool fallback) {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isBool() ? v.toBool() : fallback;
        };
        const auto intAt = [&o](const char* key, int fallback) {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isDouble() ? v.toInt() : fallback;
        };

        const QString name = o.value("name").toString();
        if (!name.isEmpty()) m_name = name;
        m_registration   = boolAt("registration", m_registration);
        m_anonymousJoin  = boolAt("anonymous_join", m_anonymousJoin);
        const QString rooms = o.value("anonymous_rooms").toString();
        if (!rooms.isEmpty()) m_anonymousRooms = rooms;
        m_minPasswordLen = intAt("min_password_len", m_minPasswordLen);
        m_codeMinLen     = intAt("code_min_len", m_codeMinLen);
        m_maxAliases     = intAt("max_aliases_per_room", m_maxAliases);
        m_chatImageMaxKb = intAt("chat_image_max_kb", m_chatImageMaxKb);
        m_maxPersonalRooms = intAt("max_personal_rooms", m_maxPersonalRooms);
        m_maxScreenShares = intAt("max_screen_shares", m_maxScreenShares);
        m_logsNames      = boolAt("logs_names", m_logsNames);

        const QJsonObject ver = o.value("version").toObject();
        m_versionCommit = ver.value("commit").toString();
        m_versionModified = ver.value("modified").toBool();

        m_loaded = true;
        emit changed();
        });
}
