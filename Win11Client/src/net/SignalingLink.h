#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <atomic>

class QWebSocket;

// Транспорт сигналинга: одно WebSocket-соединение с комнатой, живущее на
// ОТДЕЛЬНОМ потоке. Состояние комнаты (участники, чат, фазы) осталось в
// SignalingClient на GUI-потоке — здесь только байты.
//
// Зачем поток. Кадры звука идут каждые 20 мс, и раньше они попадали в
// аудиодвижок через событийный цикл GUI-потока. А тот встаёт: пока тянут рамку
// окна, Windows крутит свой модальный цикл размера (таймеры и посланные события
// Qt в нём голодают), а Qt Quick на каждом шаге блокирует GUI-поток до конца
// отрисовки кадра. Двести миллисекунд такой паузы — и звук приезжает пачкой,
// джиттер-буфер к тому времени пуст, а в динамиках дыра. На своём потоке сокет
// читает и пишет по расписанию сети, что бы ни делал интерфейс.
//
// Все слоты вызываются queued'ом с чужих потоков; всё, что читается снаружи без
// сигнала (состояние, счётчики), — атомики.
class SignalingLink : public QObject {
    Q_OBJECT
public:
    explicit SignalingLink(QObject* parent = nullptr);
    ~SignalingLink() override;

    // ---- читается с ЛЮБОГО потока ----
    bool isConnected() const { return m_connected.load(std::memory_order_relaxed); }
    // Неотправленный остаток в сокете (байты) — backpressure §5.5.
    qint64 bufferedBytes() const { return m_buffered.load(std::memory_order_relaxed); }
    // Входящие байты: прочитать и обнулить. Так их забирает MediaStats раз в
    // секунду — считать их сигналом на каждый кадр значило бы будить GUI-поток
    // полсотни раз в секунду ради одного сложения.
    qint64 takeRxBytes() { return m_rxBytes.exchange(0, std::memory_order_relaxed); }

public slots:
    // URL и кука сессии приходят значениями: ApiClient живёт на GUI-потоке, и
    // спрашивать его отсюда нельзя.
    void open(const QUrl& url, const QString& sessionToken);
    void sendText(const QString& text);
    void sendBinary(const QByteArray& frame);
    void close();               // закрыть (сокет доживёт до disconnected)
    void closeGracefully();     // выход из комнаты: дать уйти close-фрейму
    void shutdown();            // снос сокета перед остановкой потока

signals:
    void opened();
    void closed();
    void textMessage(const QString& text);
    // Полоса звука (AUDIO_CODED / SCREEN_AUDIO) — сразу на аудиопоток, минуя
    // GUI. Ради этой строчки всё и затевалось.
    void audioFrame(const QByteArray& frame);
    // Полосы видео (камера и демонстрация, включая legacy-JPEG) — на потоки
    // декодирования. Обе полосы идут одним сигналом, воркеры разбирают свою по
    // типу: подписчиков двое, и заводить ради этого два сигнала незачем.
    void videoFrame(const QByteArray& frame);
    // Всё остальное (KEYFRAME_REQ и незнакомые типы) — на GUI-поток.
    void binaryFrame(const QByteArray& frame);

private:
    void onBinary(const QByteArray& frame);
    void detachSocket();        // отвязать и отпустить текущий сокет

    QWebSocket* m_ws = nullptr;
    std::atomic<bool>   m_connected{ false };
    std::atomic<qint64> m_buffered{ 0 };
    std::atomic<qint64> m_rxBytes{ 0 };
};
