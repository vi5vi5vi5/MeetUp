#pragma once

// Приём тома Ferry — порт Server/web/assets/js/receiver.js из репозитория
// Ferry на Qt. Порядок тот же, что у веба и CLI: доказать владение ключом,
// hello с картой принятого, чанки в любом порядке с проверкой каждого против
// списка хешей, отчёты have, отдача чанков тем, кто пришёл позже, и докачка
// с того же места после обрыва или перезапуска.
//
// Живёт в своём потоке (FerryHub держит один на все приёмы): расшифровка,
// BLAKE3 и запись чанка в 4 МиБ — миллисекунды, и делать их на GUI-потоке
// значило бы дёргать видео звонка. Наружу — только сигналы со снимком
// состояния, не чаще четырёх раз в секунду.

#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>

#include <vector>

#include "FerryDisk.h"
#include "FerryVolume.h"
#include "core/ChunkSet.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;

class FerryReceiver : public QObject
{
    Q_OBJECT
public:
    struct Setup
    {
        QString origin;        // https://host[:порт]
        QString id;
        FerryMeta meta;
        FerryVolume volume;
        QString partPath;      // недокачка: файл или каталог «<имя>.ferry-part»
        QString finalDir;      // куда положить готовое
        QString finalPath;     // том уже собран раньше — только раздавать
        QString stateFile;     // карта принятого и токен возврата (JSON)
        QString name;          // как нас увидит отправитель
        bool seeding = true;   // раздавать ли скачанное другим
        bool connect = true;   // идти ли к релею сразу (готовый том — по просьбе)
    };

    explicit FerryReceiver(Setup setup, QObject *parent = nullptr);
    ~FerryReceiver() override;

public slots:
    void start();
    // Сохранить карту, попрощаться с релеем, отпустить файлы; затем stopped().
    void stop();
    void setSeeding(bool on);
    void reconnectNow();

signals:
    // Снимок для карточки: phase, bytes, total, rate, cells, … (см. snapshot).
    void progress(const QVariantMap &snapshot);
    void failed(const QString &code, const QString &message, bool retry);
    void done(const QString &finalPath);
    void stopped();

private:
    struct RateMeter
    {
        qint64 last = 0;
        qint64 bytes = 0;
        double value = 0;
        void add(qint64 n, qint64 now);
        double current(qint64 now) const;
    };

    qint64 now() const { return m_clock.elapsed(); }
    quint64 count() const { return m_s.volume.plan.chunkCount; }
    const uint8_t *prefix() const;

    bool restore();
    void connectRelay();
    void onChallenge(QNetworkReply *reply);
    void sendHello(const QByteArray &challenge, const QByteArray &proof, bool wantStream);
    void socketGone(QWebSocket *ws);
    void abandonSocket();
    void closeSocket();
    void scheduleReconnect();
    void sendJson(const QVariantMap &obj);

    void onText(const QString &text);
    void onBinary(const QByteArray &frame);
    void onHashSegment(const QVariantMap &v);
    void onHashesDone(const QVariantMap &v);
    bool hashMatches(quint64 index, const uint8_t *data, size_t len) const;
    void reject(quint64 index, const char *why);

    void housekeeping();
    void verifyPending();
    void reportHave();
    void saveState();
    void serve();
    void checkDone();
    bool finalizeVolume();
    void seedRefusedWith(const QString &code);

    void fail(const QString &code, const QString &message = QString());
    void setPhase(const QString &phase);
    void emitProgress();
    QVariantMap snapshot() const;

    Setup m_s;
    std::vector<uint8_t> m_hashes;     // count * 32
    quint64 m_hashed = 0;
    bool m_hashesComplete = false;

    ferry::ChunkSet m_have;
    ferry::ChunkSet m_pending;         // на диске, но ещё не сверено
    ferry::ChunkSet m_late;            // приехало второй волной
    ferry::ChunkSet m_serveQueue;      // что просили отдать
    ferry::Hash32 m_storedRoot{};
    bool m_hasStoredRoot = false;

    FerryDisk m_disk;
    QNetworkAccessManager *m_nam = nullptr;
    QWebSocket *m_ws = nullptr;
    QTimer *m_tick = nullptr;
    QTimer *m_reconnect = nullptr;
    QElapsedTimer m_clock;

    QString m_phase = QStringLiteral("idle");
    QString m_rejoin;
    bool m_accepted = false;
    bool m_waitingApproval = false;
    bool m_ranges = false;
    bool m_finished = false;
    bool m_stopped = false;
    bool m_failed = false;
    bool m_seeding = true;
    bool m_resumed = false;
    bool m_dirty = false;
    bool m_serving = false;
    int m_reconnectAttempt = 0;
    qint64 m_reconnectAt = 0;
    qint64 m_sentHave = -1;
    qint64 m_lastAck = 0;
    qint64 m_lastMessageAt = 0;
    qint64 m_socketOpenedAt = 0;
    qint64 m_lastSave = -100000;
    qint64 m_lastEmit = -100000;
    quint64 m_received = 0;
    quint64 m_sessionStart = 0;
    qint64 m_joinIndex = -2;           // -2 — ещё не видели ни одного чанка
    quint64 m_liveFront = 0;
    int m_badInARow = 0;
    qint64 m_outstanding = 0;          // отдано в сокет, но ещё не ушло
    qint64 m_served = 0;
    qint64 m_servedBytes = 0;
    QString m_seedRefused;
    QString m_relayState;
    bool m_senderAway = false;
    qint64 m_senderWaitUntil = 0;
    qint64 m_usesLeft = -2;            // -2 — не знаем
    double m_srcWindow = 0, m_srcPeers = 0, m_srcSender = 0;
    QString m_failCode;
    QString m_failText;
    QString m_finalPath;
    RateMeter m_meter;
};
