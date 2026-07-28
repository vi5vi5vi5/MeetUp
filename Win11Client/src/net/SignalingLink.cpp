#include "SignalingLink.h"
#include "Protocol.h"
#include <QWebSocket>
#include <QNetworkRequest>
#include <QTimer>
#include <QSslError>
#include <QDebug>

SignalingLink::SignalingLink(QObject* parent) : QObject(parent) {}

SignalingLink::~SignalingLink() { /* сокет — child, удалится сам */ }

void SignalingLink::open(const QUrl& url, const QString& sessionToken) {
    // Пересоздаём сокет на каждую попытку (переиспользовать закрытый нельзя).
    if (m_ws) { m_ws->deleteLater(); m_ws = nullptr; }
    m_connected.store(false, std::memory_order_relaxed);
    m_buffered.store(0, std::memory_order_relaxed);

    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_ws, &QWebSocket::connected, this, [this]() {
        m_connected.store(true, std::memory_order_relaxed);
        emit opened();
        });
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        m_connected.store(false, std::memory_order_relaxed);
        emit closed();
        });
    connect(m_ws, &QWebSocket::textMessageReceived, this, &SignalingLink::textMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &SignalingLink::onBinary);
    // Учёт неотправленного: sendBinary прибавляет, подтверждения ОС — вычитают.
    // bytesWritten считает и служебные байты WS-фрейминга, поэтому кламп в ноль.
    connect(m_ws, &QWebSocket::bytesWritten, this, [this](qint64 n) {
        qint64 was = m_buffered.load(std::memory_order_relaxed);
        while (!m_buffered.compare_exchange_weak(was, qMax<qint64>(0, was - n),
                                                 std::memory_order_relaxed)) {}
        });
    // Диагностика: молча падающий сокет выглядит как вечное «переподключение»,
    // а причина (DNS, TLS, отказ сервера) видна только здесь.
    connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        qWarning() << "SignalingLink: ошибка сокета:" << e << m_ws->errorString();
        });
    connect(m_ws, &QWebSocket::sslErrors, this, [](const QList<QSslError>& errors) {
        for (const QSslError& e : errors)
            qWarning() << "SignalingLink: TLS:" << e.errorString();
        });

    // Хендшейк-запрос: URL + (для аккаунта) кука сессии.
    QNetworkRequest req{ url };
    if (!sessionToken.isEmpty())
        req.setRawHeader("Cookie", QByteArray("meetup_session=") + sessionToken.toUtf8());
    m_ws->open(req);
}

// Развилка на входе: звук уходит прямо на аудиопоток, остальное — на GUI.
// Тип лежит в первом байте заголовка (§5.3), разбирать кадр целиком незачем.
void SignalingLink::onBinary(const QByteArray& d) {
    m_rxBytes.fetch_add(d.size(), std::memory_order_relaxed);
    const quint8 type = d.isEmpty() ? 0 : quint8(d[0]);
    if (type == Proto::AUDIO_CODED || type == Proto::SCREEN_AUDIO) emit audioFrame(d);
    else                                                           emit binaryFrame(d);
}

void SignalingLink::sendText(const QString& text) {
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(text);
}

void SignalingLink::sendBinary(const QByteArray& frame) {
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        m_buffered.fetch_add(frame.size(), std::memory_order_relaxed);
        m_ws->sendBinaryMessage(frame);
    }
}

void SignalingLink::close() {
    if (m_ws) m_ws->close();
}

// Выход из комнаты. Удаление сразу после close() рвёт соединение, не дав уйти
// close-фрейму: сервер видит не «участник вышел», а «пропал», и выносит нас из
// комнаты только по таймауту. Отпускаем сокет по disconnected, со страховкой на
// случай, если ответ не придёт вовсе.
void SignalingLink::closeGracefully() {
    if (!m_ws) return;
    QWebSocket* ws = m_ws;
    detachSocket();
    connect(ws, &QWebSocket::disconnected, ws, &QObject::deleteLater);
    QTimer::singleShot(3000, ws, [ws]() { ws->deleteLater(); });
    ws->close();
}

void SignalingLink::shutdown() {
    if (!m_ws) return;
    QWebSocket* ws = m_ws;
    detachSocket();
    delete ws;              // поток вот-вот встанет: deleteLater уже не сработает
}

void SignalingLink::detachSocket() {
    disconnect(m_ws, nullptr, this, nullptr);   // дохлый сокет нас больше не касается
    m_ws = nullptr;
    m_connected.store(false, std::memory_order_relaxed);
    m_buffered.store(0, std::memory_order_relaxed);
}
