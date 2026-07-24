#include "LinkController.h"
#include "ApiClient.h"
#include "AuthController.h"
#include "RoomController.h"
#include "../SysBridge.h"
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>

LinkController::LinkController(ApiClient* api, AuthController* auth,
                               RoomController* rooms, SysBridge* sys, QObject* parent)
    : QObject(parent), m_api(api), m_auth(auth), m_rooms(rooms), m_sys(sys) {
    // Ответ на checkSession() после смены адреса: есть ли у нас аккаунт ТАМ.
    connect(auth, &AuthController::sessionChecked, this, [this](bool ok) {
        if (!m_restoring && m_wantCode.isEmpty()) return;   // спрашивали не мы
        setSwitching(false);

        if (m_restoring) {                 // вернулись на свой сервер
            m_restoring = false;
            // Аккаунта на нём (уже) нет — честно уходим на экран входа: теперь
            // switching снят, и Main.qml сигнал не проигнорирует.
            if (!ok) m_auth->forgetSession();
            return;
        }

        const QString code = m_wantCode;
        m_wantCode.clear();
        // Аккаунта на этом сервере нет — имя спросит гейт конференции, а мы
        // подскажем ему то, под которым человек был на прежнем сервере.
        if (!ok || m_auth->displayName().isEmpty()) {
            setSuggestedName(m_homeName);
            setNotice(switchNotice());
        }
        enterRoom(code);
        });
}

void LinkController::setError(const QString& text) {
    if (m_errorText == text) return; m_errorText = text; emit errorTextChanged();
}
void LinkController::setPendingKey(const QString& key) {
    if (m_pendingKey == key) return; m_pendingKey = key; emit pendingKeyChanged();
}
void LinkController::setSwitching(bool v) {
    if (m_switching == v) return; m_switching = v; emit switchingChanged();
}
void LinkController::setHomeServer(const QString& base) {
    if (m_homeServer == base) return; m_homeServer = base; emit homeServerChanged();
}
void LinkController::setSuggestedName(const QString& name) {
    if (m_suggestedName == name) return; m_suggestedName = name; emit suggestedNameChanged();
}
void LinkController::setNotice(const QString& text) {
    if (m_notice == text) return; m_notice = text; emit noticeChanged();
}

// Вход по ссылке устроен как в вебе: экран конференции открывается сразу, а
// имя (и пароль, если комната закрыта) спрашивает гейт уже на нём. Поэтому
// enter(), а не joinByCode(): проверять комнату отдельным запросом незачем —
// обо всех отказах расскажет сам сокет, и ровно теми же словами, что вебу.
void LinkController::enterRoom(const QString& code) {
    m_rooms->enter(code, m_auth->displayName());
}

// Схема + хост + порт в сравнимом виде. Хост обязательно в ACE: с пустым
// IDN-whitelist (см. main.cpp) QUrl и так держит его в punycode, но ссылку нам
// дают откуда угодно — мит-ап.рф и xn----8sbwpsp.xn--p1ai должны совпасть.
QString LinkController::normalizeBase(const QString& base) {
    const QUrl u(base);
    QString scheme = u.scheme().toLower();
    if (scheme.isEmpty()) scheme = QStringLiteral("https");
    const QString host = QString::fromLatin1(QUrl::toAce(u.host())).toLower();
    int port = u.port();
    if (port <= 0) port = (scheme == QLatin1String("https")) ? 443 : 80;
    return scheme + "://" + host + ":" + QString::number(port);
}

QVariantMap LinkController::parse(const QString& text) const {
    QVariantMap r{ {"ok", false}, {"code", ""}, {"key", ""}, {"base", ""},
                   {"sameServer", true}, {"error", ""} };

    const QString s = text.trimmed();
    if (s.isEmpty()) { r["error"] = "Вставьте ссылку на конференцию."; return r; }

    // Голый код комнаты: коды сервера — [a-z0-9_-], ни точек, ни слэшей,
    // ни двоеточий в них быть не может. Значит, это не адрес, а код.
    if (!s.contains('/') && !s.contains('.') && !s.contains(':')) {
        r["ok"] = true;
        r["code"] = s;
        return r;                       // sameServer уже true
    }

    // Схему дописываем как SysBridge::setServer — по умолчанию https.
    QString raw = s;
    if (!raw.startsWith("http://", Qt::CaseInsensitive)
        && !raw.startsWith("https://", Qt::CaseInsensitive))
        raw = "https://" + raw;

    const QUrl url(raw);
    if (!url.isValid() || url.host().isEmpty()) {
        r["error"] = "Не похоже на ссылку на конференцию.";
        return r;
    }

    const QString code = QUrlQuery(url).queryItemValue("room", QUrl::FullyDecoded);
    if (code.isEmpty()) {
        r["error"] = "В ссылке нет кода комнаты.";
        return r;
    }

    // Ключ E2E живёт во фрагменте (#k=…) — эта часть адреса на сервер не
    // уходит вовсе. Формат тот же, что читает веб (conference.html).
    static const QRegularExpression keyRe("(?:^|&)k=([A-Za-z0-9_-]+)");
    const auto m = keyRe.match(url.fragment());
    if (m.hasMatch()) r["key"] = m.captured(1);

    QString base = url.scheme() + "://" + url.host();
    if (url.port() > 0) base += ":" + QString::number(url.port());

    r["ok"] = true;
    r["code"] = code;
    r["base"] = base;
    r["sameServer"] = (normalizeBase(base) == normalizeBase(m_api->baseUrl()));
    return r;
}

// Текст плашки анонимного лобби: объясняем, почему адрес вдруг другой.
QString LinkController::switchNotice() const {
    const QString to = m_sys->hostOf(m_api->baseUrl());
    const QString from = m_sys->hostOf(m_homeServer);
    QString s = "Ссылка ведёт на " + to + " — сервер переключён.";
    if (!m_homeName.isEmpty())
        s += " Входим гостем, имя взято из вашего аккаунта на " + from + ".";
    else
        s += " Укажите имя, чтобы войти гостем.";
    s += " После конференции вернём " + from + ".";
    return s;
}

void LinkController::open(const QString& text) {
    const QVariantMap r = parse(text);
    if (!r.value("ok").toBool()) { setError(r.value("error").toString()); return; }

    setError("");
    m_rooms->clearError();
    setPendingKey(r.value("key").toString());
    const QString code = r.value("code").toString();

    if (r.value("sameServer").toBool()) {
        setSuggestedName("");        // подсказывать нечего: либо аккаунт, либо гейт
        setNotice("");
        enterRoom(code);
        return;
    }

    // Чужой сервер. Прежний адрес и имя запоминаем ДО переключения: имя уедет
    // в лобби подстановкой, адрес — вернётся после конференции. Сессию гасим
    // только локально: кука прежнего сервера лежит отдельно (SessionCookieJar)
    // и нужна, чтобы возврат не спрашивал пароль.
    m_homeName = m_auth->displayName();
    setHomeServer(m_api->baseUrl());
    m_wantCode = code;
    m_restoring = false;
    setSwitching(true);
    if (!m_homeName.isEmpty()) m_auth->forgetSession();
    m_sys->setServer(r.value("base").toString());
    m_auth->checkSession();          // вдруг у нас и здесь есть аккаунт
}

void LinkController::roomLeft() {
    setPendingKey("");               // предупреждение про E2E — только на ту комнату
    setSuggestedName("");
    setNotice("");
    if (m_homeServer.isEmpty()) return;

    const QString back = m_homeServer;
    setHomeServer("");
    m_homeName.clear();
    m_wantCode.clear();
    if (normalizeBase(back) == normalizeBase(m_api->baseUrl())) return;   // уже дома

    m_restoring = true;
    setSwitching(true);              // Main.qml не дёргается на промежуточный выход
    m_auth->forgetSession();         // профиль чужого сервера — в ноль
    m_sys->setServer(back);          // setBaseUrl вернёт и сохранённую куку
    m_auth->checkSession();          // …а она вернёт аккаунт без пароля
}
