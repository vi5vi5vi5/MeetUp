#include "SignalingLink.h"
#include "Protocol.h"
#include <QWebSocket>
#include <QNetworkRequest>
#include <QTimer>
#include <QDateTime>
#include <QSslError>
#include <QDebug>

// Во сколько байт обходится сообщение с таким payload на выходе из сокета.
//
// Это не педантизм, а условие работоспособности всего учёта очереди. Раньше
// sendBinary прибавлял размер payload, а bytesWritten вычитал то, что реально
// ушло, — то есть всегда больше на служебные байты кадра. Разница мелкая, но
// вычитается она из очереди: при умеренном заторе (канал держит процентов
// семьдесят потока) накопленного за секунду оверхеда хватало, чтобы счётчик
// сполз в ноль и остался там. А по нему решается и отбраковка кадров, и
// ступень качества демонстрации — оба механизма молчали ровно тогда, когда
// были нужнее всего. Это и видно в «Диагностике»: 0 % пропуска у ведущего при
// полном битрейте на канале, который его очевидно не держит.
//
// Клиентский кадр WebSocket: 2 байта базового заголовка, расширенная длина
// (0/2/8) и 4 байта маски — клиент маскирует всегда (RFC 6455 §5.3).
//
// Цену TLS сюда НЕ добавляем, хотя соединение и бывает wss. Причина в том, как
// устроен QSslSocket: bytesWritten он отдаёт по НЕзашифрованным байтам, то
// есть ровно по тем, что мы в него положили. Если бы это оказалось не так,
// ошибка вышла бы в безопасную сторону — счётчик занижен, а не завышен, и его
// подберёт ресинхрон. Завышение же не лечится ничем: очередь росла бы сама по
// себе, и мы навсегда решили бы, что канал в заторе.
static qint64 wireSize(qint64 payload) {
    qint64 n = payload + 2 + 4;
    if (payload >= 126) n += (payload > 0xFFFF) ? 8 : 2;
    return n;
}

SignalingLink::SignalingLink(QObject* parent) : QObject(parent) {}

SignalingLink::~SignalingLink() { /* сокет — child, удалится сам */ }

// Страховка от дрейфа оценки. Очередь мы ведём разностью, а разность копит
// любую систематическую ошибку — поэтому раз в секунду сверяемся с фактом:
// если за окно сокет отдал примерно всё, что мы в него положили, значит
// очереди нет, какой бы ни была накопленная арифметика.
//
// «Примерно» — с запасом в 15 %, и он односторонний. Недооценить размер
// сообщения мы можем (см. wireSize), переоценить — нет, так что допуск нужен
// только сверху. Затор при этом виден: если канал держит меньше девяти
// десятых потока, условие не выполнится и счётчик продолжит расти.
//
// По часам, а не по таймеру: объект создаётся на GUI-потоке и переезжает на
// свой уже готовым, так что запущенный в конструкторе таймер принадлежал бы
// не тому потоку. Зовётся из тех же мест, где очередь и меняется, — значит
// сверка случается ровно тогда, когда есть что сверять.
void SignalingLink::resyncQueue() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_resyncAt == 0) { m_resyncAt = now; return; }
    if (now - m_resyncAt < 1000) return;
    m_resyncAt = now;
    if (m_wirePut > 0 && m_wireOut * 100 >= m_wirePut * 87) {
        m_wireQueued = 0;
        m_buffered.store(0, std::memory_order_relaxed);
    }
    m_wirePut = 0;
    m_wireOut = 0;
}

void SignalingLink::open(const QUrl& url, const QString& sessionToken) {
    // Пересоздаём сокет на каждую попытку (переиспользовать закрытый нельзя).
    if (m_ws) { m_ws->deleteLater(); m_ws = nullptr; }
    m_connected.store(false, std::memory_order_relaxed);
    m_buffered.store(0, std::memory_order_relaxed);
    m_wireQueued = m_wirePut = m_wireOut = m_resyncAt = 0;

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
    // Учёт неотправленного: отправка прибавляет оценку размера на проводе,
    // подтверждения ОС — вычитают ровно те же единицы (см. wireSize).
    connect(m_ws, &QWebSocket::bytesWritten, this, [this](qint64 n) {
        m_wireOut += n;
        m_wireQueued = qMax<qint64>(0, m_wireQueued - n);
        m_buffered.store(m_wireQueued, std::memory_order_relaxed);
        resyncQueue();
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

// Развилка на входе: каждая полоса уезжает на свой поток, и только служебные
// кадры — на GUI. Тип лежит в первом байте заголовка (§5.3), разбирать кадр
// целиком незачем — это делает уже тот, кто будет его декодировать.
void SignalingLink::onBinary(const QByteArray& d) {
    m_rxBytes.fetch_add(d.size(), std::memory_order_relaxed);
    switch (d.isEmpty() ? 0 : quint8(d[0])) {
    case Proto::AUDIO_CODED:
    case Proto::SCREEN_AUDIO:
        emit audioFrame(d);
        break;
    case Proto::VIDEO_CODED:
    case Proto::SCREEN_CODED:
    case Proto::VIDEO_JPEG:
    case Proto::SCREEN_JPEG:
        emit videoFrame(d);
        break;
    default:
        emit binaryFrame(d);
        break;
    }
}

void SignalingLink::sendText(const QString& text) {
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        // Текст тоже занимает канал, и в очередь его класть надо: чат с
        // картинкой — это сотни килобайт, то есть заметно больше любого кадра.
        const qint64 wire = wireSize(text.toUtf8().size());
        m_wireQueued += wire;
        m_wirePut += wire;
        m_buffered.store(m_wireQueued, std::memory_order_relaxed);
        m_ws->sendTextMessage(text);
    }
}

void SignalingLink::sendBinary(const QByteArray& frame) {
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        const qint64 wire = wireSize(frame.size());
        m_wireQueued += wire;
        m_wirePut += wire;
        m_buffered.store(m_wireQueued, std::memory_order_relaxed);
        resyncQueue();
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
    m_wireQueued = m_wirePut = m_wireOut = m_resyncAt = 0;
}
