#pragma once

// Отправка тома Ferry — порт Server/web/assets/js/send.worker.js (и
// Cli/Sender.cpp) из репозитория Ferry на Qt.
//
// Ключ рождается здесь и уходит только наружу, в ссылку; на релей идёт V, из
// которого K обратно не вывести. Хеши считает отдельный поток (FerryHasher)
// и присылает пачками; при «хешах на лету» ссылка готова сразу, а список
// досылается сегментами. Чанк не уходит раньше своего хеша — релей это
// проверяет, и на этом правиле держится всё, что получатель знает о
// целостности.
//
// Живёт в потоке ferry-io рядом с приёмами; наружу — сигналы.

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <map>
#include <vector>

#include "core/ChunkSet.h"
#include "core/Chunker.h"
#include "core/Crypto.h"
#include "core/Manifest.h"
#include "core/VolumeLayout.h"
#include "FerryDisk.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;

// BLAKE3 каждого чанка тома, по порядку, в своём потоке: чтение с диска на
// десятки гигабайт не должно стоять ни в отправке, ни в интерфейсе.
class FerryHasher : public QThread
{
    Q_OBJECT
public:
    FerryHasher(const ferry::Manifest &manifest, std::vector<QString> sources, ferry::ChunkPlan plan,
                QObject *parent = nullptr);
    void cancel() { m_stop = true; }

signals:
    void hashed(quint64 from, const QByteArray &hashes, quint64 bytes);
    void failed(const QString &message);
    void finishedAll();

protected:
    void run() override;

private:
    ferry::Manifest m_manifest;
    ferry::VolumeLayout m_layout;
    std::vector<QString> m_sources;
    ferry::ChunkPlan m_plan;
    std::atomic<bool> m_stop{false};
};

class FerrySender : public QObject
{
    Q_OBJECT
public:
    struct Policy
    {
        qint64 uses = -1;            // -1 — без лимита
        qint64 ttlMs = 0;            // 0 — умолчание релея
        int maxConcurrent = 0;       // 0 — умолчание релея
        bool requireApproval = false;
        bool leaveOpen = false;      // «раздача без меня»
    };
    struct Setup
    {
        QString origin;              // релей: https://host[:порт]
        ferry::Manifest manifest;    // опись без корня
        std::vector<QString> sources;
        Policy policy;
        bool password = false;       // ключ выведен из фразы (Argon2id)
        ferry::Key32 passwordKey{};
        QByteArray salt;
    };

    explicit FerrySender(Setup setup, QObject *parent = nullptr);
    ~FerrySender() override;

public slots:
    void start();
    // Пульт владельца — то же, что у страницы Ferry.
    void approve(qint64 receiver, bool ok);
    void approveAll();
    void setPolicy(const QVariantMap &policy);
    // revoke — «Закрыть доступ» для всех; иначе — уйти самим (с «раздачей без
    // меня» том останется у скачавших).
    void stop(bool revoke);

signals:
    // Раздача заведена и принята релеем: ссылку можно класть в чат.
    void ready(const QString &link, const QString &ownerLink, const QString &id,
               const QString &ownerToken);
    void progress(const QVariantMap &snapshot);
    void knock(qint64 receiver, const QString &name);
    void knockGone(qint64 receiver);
    void relayState(const QVariantMap &state);
    void failed(const QString &code, const QString &message);
    void closed();

private:
    qint64 now() const { return m_clock.elapsed(); }
    const uint8_t *prefix() const { return m_noncePrefix; }

    void createDraft();
    void onCreated(QNetworkReply *reply);
    void beginOffer();
    void openSocket(bool resume);
    void onText(const QString &text);
    void onAccepted();
    void socketClosed(QWebSocket *ws);
    void lost();
    void tryResume();
    void adopt(const QVariantMap &v);
    void rememberNeed(const QVariantMap &v);

    void onHashed(quint64 from, const QByteArray &hashes, quint64 bytes);
    void onHashingDone();
    QByteArray sealMeta(uint64_t label, const uint8_t *plain, size_t len) const;
    void sendFinalMeta(QJsonObject *offer);
    bool announce();
    qint64 pick();
    void fill();
    void kick();
    void tick();
    void sendJson(const QJsonObject &o);
    void beacon(const char *what);

    void fail(const QString &code, const QString &message = QString());
    void setPhase(const QString &phase);
    void emitProgress(bool force);

    Setup m_s;
    ferry::ChunkPlan m_plan;
    ferry::VolumeLayout m_layout;
    ferry::TransferKeys m_keys;
    ferry::Key32 m_master{};
    uint8_t m_noncePrefix[4] = {};
    QString m_keyText;
    QString m_link;
    QString m_ownerLink;
    QByteArray m_offer;                      // offer, ждущий открытия сокета

    FerryHasher *m_hasher = nullptr;
    std::vector<uint8_t> m_hashes;
    quint64 m_hashed = 0;
    quint64 m_hashBytes = 0;
    bool m_hashingDone = false;
    quint64 m_announced = 0;
    bool m_hashesDoneSent = false;
    qint64 m_lastSegment = 0;
    std::map<quint64, quint64> m_segments;   // from -> count: повтор обязан совпасть

    QNetworkAccessManager *m_nam = nullptr;
    QWebSocket *m_ws = nullptr;
    QTimer *m_tick = nullptr;
    QElapsedTimer m_clock;
    QString m_phase = QStringLiteral("idle");

    QString m_id;
    QString m_ownerToken;
    QString m_publicUrl;
    QStringList m_features;
    bool m_stream = false;
    bool m_canResume = false;
    bool m_wantApproval = false;
    bool m_accepted = false;
    bool m_finished = false;
    bool m_reconnecting = false;
    bool m_waitingHashes = false;
    qint64 m_draftAt = 0;
    qint64 m_lastHeard = 0;
    qint64 m_resumeDeadline = 0;
    int m_resumeDelay = 500;
    QString m_lastError;

    ferry::ChunkSet m_live;
    ferry::ChunkSet m_backfill;
    quint64 m_liveHint = 0;
    quint64 m_backfillHint = 0;
    quint64 m_sentLive = 0;
    quint64 m_sentBackfill = 0;
    ferry::ChunkSet m_sent;
    quint64 m_sentBytes = 0;
    quint64 m_distinctBytes = 0;
    qint64 m_outstanding = 0;
    FerryDisk m_reader;                      // чтение чанков для отправки

    struct Meter
    {
        qint64 last = 0, bytes = 0;
        double value = 0;
        void add(qint64 n, qint64 t);
        double read(qint64 t) const;
    } m_meter, m_hashMeter;
    qint64 m_lastProgress = -100000;
    QVariantList m_receivers;
    QVariantMap m_state;
    bool m_pumping = false;
};
