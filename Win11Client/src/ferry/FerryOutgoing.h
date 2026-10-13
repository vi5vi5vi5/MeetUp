#pragma once

// Отправка Ferry глазами интерфейса.
//
// FerryDraft — «лист погрузки»: что выбрано (опись собирается в фоне),
// какой релей, что он умеет. Из него — FerrySend.
//
// FerrySend — своя раздача, к которой привязана карточка отправителя в чате:
// ход хешей, получатели с ролями, стук в дверь, условия на ходу, квитанция.
// Живёт, пока открыто приложение — и после выхода из звонка тоже: ссылка уже
// в чате, и обрывать раздачу из-за смены экрана незачем.

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include "FerryCargo.h"
#include "FerrySender.h"

class FerryHub;
class QNetworkReply;

class FerryDraft : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool tree READ tree NOTIFY changed)
    Q_PROPERTY(double files READ files NOTIFY changed)
    Q_PROPERTY(double dirs READ dirs NOTIFY changed)
    Q_PROPERTY(double total READ total NOTIFY changed)
    // Первые позиции описи для листа: [{name, size, dir}].
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QStringList skipped READ skipped NOTIFY changed)
    // Релей: адрес, откуда он (server — из настроек сервера MeetUp, own — свой)
    // и что о себе сказал (/api/health).
    Q_PROPERTY(QString relay READ relay NOTIFY relayChanged)
    Q_PROPERTY(QString relaySource READ relaySource NOTIFY relayChanged)
    Q_PROPERTY(QString relayStatus READ relayStatus NOTIFY relayChanged)   // "" | checking | ok | bad
    Q_PROPERTY(int relayMaxConcurrent READ relayMaxConcurrent NOTIFY relayChanged)
    Q_PROPERTY(bool relayPolicy READ relayPolicy NOTIFY relayChanged)
    Q_PROPERTY(bool relayPassword READ relayPassword NOTIFY relayChanged)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)

public:
    FerryDraft(FerryHub *hub, QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    QString error() const { return m_error; }
    QString name() const { return QString::fromStdString(m_cargo.manifest.name); }
    bool tree() const { return m_cargo.manifest.isTree(); }
    double files() const { return double(m_cargo.files); }
    double dirs() const { return double(m_cargo.dirs); }
    double total() const { return double(m_cargo.manifest.total); }
    QVariantList items() const;
    QStringList skipped() const { return m_cargo.skipped; }
    QString relay() const { return m_relay; }
    QString relaySource() const { return m_relaySource; }
    QString relayStatus() const { return m_relayStatus; }
    int relayMaxConcurrent() const { return m_relayMax; }
    bool relayPolicy() const { return m_relayPolicy; }
    bool relayPassword() const { return m_relayPassword; }
    bool sending() const { return m_sending; }

    // Добавить к грузу ещё (кнопки «+ файлы», «+ папка» листа).
    Q_INVOKABLE void add(const QVariant &urls);
    // Другой релей: адрес вида https://имя[:порт].
    Q_INVOKABLE bool setRelay(const QString &origin);
    // Что не так с фразой; пусто — годится (те же правила, что у CLI и веба).
    Q_INVOKABLE QString passwordProblem(const QString &phrase) const;
    // options: uses (-1 — без лимита), ttlHours, maxConcurrent,
    // requireApproval, leaveOpen, password (пусто — по ключу).
    Q_INVOKABLE void send(const QVariantMap &options);

    void scan(const QStringList &paths);

signals:
    void changed();
    void relayChanged();
    void sent(QObject *send);

private:
    void probeRelay();
    void launch(const QVariantMap &options, bool password, const ferry::Key32 &key, const QByteArray &salt);

    FerryHub *m_hub;
    QStringList m_paths;
    FerryCargo m_cargo;
    bool m_scanning = false;
    int m_scanSerial = 0;
    QString m_error;
    QString m_relay;
    QString m_relaySource;
    QString m_relayStatus;
    int m_relayMax = 8;
    bool m_relayPolicy = true;
    bool m_relayPassword = true;
    bool m_sending = false;
};

class FerrySend : public QObject
{
    Q_OBJECT
    // creating | hashing | offering | live | lost | closed | revoked | failed
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool tree READ tree CONSTANT)
    Q_PROPERTY(double files READ files CONSTANT)
    Q_PROPERTY(double total READ total CONSTANT)
    Q_PROPERTY(bool passwordMode READ passwordMode CONSTANT)
    Q_PROPERTY(QString link READ link NOTIFY changed)
    Q_PROPERTY(QString ownerLink READ ownerLink NOTIFY changed)
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(double hashed READ hashed NOTIFY progressChanged)
    Q_PROPERTY(double count READ count NOTIFY progressChanged)
    Q_PROPERTY(bool hashesDone READ hashesDone NOTIFY progressChanged)
    Q_PROPERTY(double sentBytes READ sentBytes NOTIFY progressChanged)
    Q_PROPERTY(double rate READ rate NOTIFY progressChanged)
    // [{id, name, progress 0..1, role: leech|catching|seed}]
    Q_PROPERTY(QVariantList receivers READ receivers NOTIFY progressChanged)
    // Откуда получатели берут байты, доли 0..1: окно релея, другие, вы.
    Q_PROPERTY(double srcWindow READ srcWindow NOTIFY progressChanged)
    Q_PROPERTY(double srcPeers READ srcPeers NOTIFY progressChanged)
    Q_PROPERTY(double srcSender READ srcSender NOTIFY progressChanged)
    // Кто ждёт у двери: [{id, name}].
    Q_PROPERTY(QVariantList knocks READ knocks NOTIFY changed)
    Q_PROPERTY(double usesLeft READ usesLeft NOTIFY changed)          // -1 — без лимита
    Q_PROPERTY(double expiresAt READ expiresAt NOTIFY changed)        // мс эпохи, 0 — не знаем
    Q_PROPERTY(bool requireApproval READ requireApproval NOTIFY changed)
    Q_PROPERTY(bool leaveOpen READ leaveOpen NOTIFY changed)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent NOTIFY changed)
    Q_PROPERTY(QString relayState READ relayState NOTIFY changed)     // active | delegated
    // Квитанция: кто получил том целиком и во сколько закрыли.
    Q_PROPERTY(QStringList delivered READ delivered NOTIFY changed)
    Q_PROPERTY(double closedAt READ closedAt NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    FerrySend(FerryHub *hub, FerrySender::Setup setup, double files, QObject *parent = nullptr);
    ~FerrySend() override;

    void start();
    QString key() const;           // origin|id — как у FerryTransfer
    bool active() const;           // раздача ещё идёт
    bool hasSeed() const;          // кто-то скачал целиком — уйти можно без потерь
    QStringList downloading() const;

    QString phase() const { return m_phase; }
    QString name() const { return QString::fromStdString(m_manifest.name); }
    bool tree() const { return m_manifest.isTree(); }
    double files() const { return m_files; }
    double total() const { return double(m_manifest.total); }
    bool passwordMode() const { return m_password; }
    QString link() const { return m_link; }
    QString ownerLink() const { return m_ownerLink; }
    QString host() const { return m_origin.section(QStringLiteral("://"), 1); }
    double hashed() const { return m_hashed; }
    double count() const { return m_count; }
    bool hashesDone() const { return m_hashesDone; }
    double sentBytes() const { return m_sentBytes; }
    double rate() const { return m_rate; }
    QVariantList receivers() const { return m_receivers; }
    double srcWindow() const { return m_srcWindow; }
    double srcPeers() const { return m_srcPeers; }
    double srcSender() const { return m_srcSender; }
    QVariantList knocks() const { return m_knocks; }
    double usesLeft() const { return double(m_usesLeft); }
    double expiresAt() const { return m_expiresAt; }
    bool requireApproval() const { return m_requireApproval; }
    bool leaveOpen() const { return m_leaveOpen; }
    int maxConcurrent() const { return m_maxConcurrent; }
    QString relayState() const { return m_relayState; }
    QStringList delivered() const { return m_delivered; }
    double closedAt() const { return m_closedAt; }
    QString errorCode() const { return m_errorCode; }
    QString errorTitle() const { return m_errorTitle; }
    QString errorText() const { return m_errorText; }

    Q_INVOKABLE void approve(double receiver, bool ok);
    Q_INVOKABLE void approveAll();
    // -1 — без лимита; иначе новый остаток.
    Q_INVOKABLE void setUses(double uses);
    // Срок «от этой минуты».
    Q_INVOKABLE void setTtlHours(double hours);
    Q_INVOKABLE void setApproval(bool on);
    Q_INVOKABLE void setLeaveOpen(bool on);
    Q_INVOKABLE void revoke();
    // Уйти самим: с «раздачей без меня» том останется у скачавших.
    Q_INVOKABLE void leave();

    void shutdown();

signals:
    void changed();
    void progressChanged();
    void ready(const QString &link);
    void knocked(double receiver, const QString &name);

private:
    void onProgress(const QVariantMap &s);
    void onState(const QVariantMap &v);
    void fetchMeta();
    void sendPolicy(const QVariantMap &p);

    FerryHub *m_hub;
    FerrySender *m_tx = nullptr;     // живёт в потоке ferry-io
    ferry::Manifest m_manifest;
    QString m_origin;
    double m_files = 0;
    bool m_password = false;
    QString m_id;
    QString m_link;
    QString m_ownerLink;
    QString m_phase = QStringLiteral("creating");
    double m_hashed = 0, m_count = 0;
    bool m_hashesDone = false;
    double m_sentBytes = 0, m_rate = 0;
    QVariantList m_receivers;
    double m_srcWindow = 0, m_srcPeers = 0, m_srcSender = 0;
    QVariantList m_knocks;
    qint64 m_usesLeft = -1;
    double m_expiresAt = 0;
    bool m_requireApproval = false;
    bool m_leaveOpen = false;
    int m_maxConcurrent = 0;
    QString m_relayState = QStringLiteral("active");
    QStringList m_delivered;
    double m_closedAt = 0;
    QString m_errorCode, m_errorTitle, m_errorText;
};
