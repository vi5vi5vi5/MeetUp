#pragma once

// Одна ссылка Ferry из чата глазами карточки под сообщением.
//
// Карточка проходит те же состояния, что веб-приёмник Ferry, плюс два своих:
//   fog         релей незнаком — ничего не спрашиваем, пока человек не нажмёт
//               (спросить релей значит показать ему свой адрес);
//   probing     спрашиваем метаданные раздачи (ничего не сжигает);
//   offer       «груз к выдаче»: имя, состав, размер — ждём «Принять»;
//   password    ссылка без ключа, раздача по паролю — спрашиваем фразу;
//   unlocking   Argon2id считает ключ из фразы (около секунды);
//   nokey       ссылка без ключа, а раздача по ключу — просим вставить его;
//   connecting / approval / receiving / reconnecting — идёт приём;
//   stopped     остановлен человеком, недокачка лежит и докачается;
//   done        том на диске;
//   failed      отказ — код, что случилось, что делать.
//
// Объект живёт всё время работы приложения (им владеет FerryHub): одна и та
// же ссылка в ленте после выхода и возвращения в звонок показывает тот же
// приём, а не начинает новый.

#include <QObject>
#include <QPointer>
#include <QVariantList>

#include "FerryVolume.h"

class FerryHub;
class FerryReceiver;
class QNetworkReply;

class FerryTransfer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(QString link READ link NOTIFY changed)
    // Накладная тома — известна после открытия ключом.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool tree READ tree NOTIFY changed)
    Q_PROPERTY(double files READ files NOTIFY changed)
    Q_PROPERTY(double dirs READ dirs NOTIFY changed)
    Q_PROPERTY(double total READ total NOTIFY changed)
    Q_PROPERTY(bool passwordMode READ passwordMode NOTIFY changed)
    // Что говорит релей о раздаче.
    Q_PROPERTY(QString relayState READ relayState NOTIFY changed)   // active | delegated | closed
    Q_PROPERTY(double usesLeft READ usesLeft NOTIFY changed)        // -1 — без лимита
    Q_PROPERTY(bool requireApproval READ requireApproval NOTIFY changed)
    // Ход приёма.
    Q_PROPERTY(double bytes READ bytes NOTIFY progressChanged)
    Q_PROPERTY(double rate READ rate NOTIFY progressChanged)        // байт/с
    Q_PROPERTY(QVariantList cells READ cells NOTIFY progressChanged)
    Q_PROPERTY(double reconnectInMs READ reconnectInMs NOTIFY progressChanged)
    Q_PROPERTY(bool senderAway READ senderAway NOTIFY progressChanged)
    Q_PROPERTY(double senderWaitMs READ senderWaitMs NOTIFY progressChanged)
    Q_PROPERTY(double count READ count NOTIFY progressChanged)
    Q_PROPERTY(bool partial READ partial NOTIFY changed)            // есть недокачка
    // Итог.
    Q_PROPERTY(QString finalPath READ finalPath NOTIFY changed)
    Q_PROPERTY(bool seeding READ seeding NOTIFY progressChanged)
    Q_PROPERTY(double servedBytes READ servedBytes NOTIFY progressChanged)
    // Отказ.
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString errorWay READ errorWay NOTIFY changed)
    Q_PROPERTY(bool retryable READ retryable NOTIFY changed)
    Q_PROPERTY(QString passwordError READ passwordError NOTIFY changed)
    Q_PROPERTY(bool declined READ declined NOTIFY changed)

public:
    FerryTransfer(FerryHub *hub, const QString &origin, const QString &id, QObject *parent = nullptr);
    ~FerryTransfer() override;

    // Ключ мог прийти не с первой ссылкой: та же раздача ниже в чате — уже
    // с частью после #.
    void adoptKey(const ferry::Key32 &key);
    void begin();   // первый показ: незнакомый релей — туман, знакомый — спросить

    QString phase() const { return m_phase; }
    QString origin() const { return m_origin; }
    QString host() const;
    QString link() const;
    QString name() const;
    bool tree() const { return m_hasVolume ? m_volume.manifest.isTree() : m_savedKind == QLatin1String("tree"); }
    double files() const { return double(m_volume.files); }
    double dirs() const { return double(m_volume.dirs); }
    double total() const { return m_hasMeta ? double(m_meta.total) : m_savedTotal; }
    bool passwordMode() const { return m_meta.password; }
    QString relayState() const { return m_relayState; }
    double usesLeft() const { return double(m_usesLeft); }
    bool requireApproval() const { return m_meta.requireApproval; }
    double bytes() const { return m_bytes; }
    double rate() const { return m_rate; }
    QVariantList cells() const { return m_cells; }
    double reconnectInMs() const { return m_reconnectInMs; }
    bool senderAway() const { return m_senderAway; }
    double senderWaitMs() const { return m_senderWaitMs; }
    double count() const { return m_count; }
    bool partial() const { return m_partial; }
    QString finalPath() const { return m_finalPath; }
    bool seeding() const { return m_seeding; }
    double servedBytes() const { return m_servedBytes; }
    QString errorCode() const { return m_errorCode; }
    QString errorTitle() const { return m_errorTitle; }
    QString errorText() const { return m_errorText; }
    QString errorWay() const { return m_errorWay; }
    bool retryable() const { return m_retryable; }
    QString passwordError() const { return m_passwordError; }
    bool declined() const { return m_declined; }

    bool active() const;          // качается или раздаёт
    QString stateFile() const;

    // «Показать сведения» у незнакомого релея; remember — доверять дальше.
    Q_INVOKABLE void probe(bool remember = false);
    Q_INVOKABLE void accept();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void unlock(const QString &phrase);
    Q_INVOKABLE bool useKey(const QString &text);
    Q_INVOKABLE void decline(bool on = true);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void openInBrowser();
    Q_INVOKABLE void setSeeding(bool on);

    // Без вопросов и без сигналов: приложение закрывается.
    void shutdown();

signals:
    void changed();
    void progressChanged();
    void finished();   // том лёг на диск — для звука и уведомления

private:
    void onMeta(QNetworkReply *reply);
    void onUnlocked(const ferry::Key32 &key, bool ok);
    void openWithKey();
    void startReceiver(bool seedOnly);
    void onProgress(const QVariantMap &s);
    void onFailed(const QString &code, const QString &message, bool retry);
    void setFailed(const QString &code, const QString &detail, bool retry);
    void setPhase(const QString &phase);
    void loadSaved();

    FerryHub *m_hub;
    QString m_origin;
    QString m_id;
    ferry::Key32 m_key{};
    bool m_hasKey = false;
    FerryMeta m_meta;
    bool m_hasMeta = false;
    FerryVolume m_volume;
    bool m_hasVolume = false;
    QPointer<QNetworkReply> m_probe;
    // Живёт в потоке приёма; удаляется только через deleteLater, и указатель
    // обнуляется здесь, на GUI-потоке, раньше — поэтому не QPointer.
    FerryReceiver *m_rx = nullptr;

    enum class After { Nothing, Accept, Seed };
    After m_after = After::Nothing;

    QString m_phase = QStringLiteral("fog");
    QString m_relayState;
    qint64 m_usesLeft = -1;
    double m_bytes = 0;
    double m_rate = 0;
    double m_count = 0;
    QVariantList m_cells;
    double m_reconnectInMs = 0;
    bool m_senderAway = false;
    double m_senderWaitMs = 0;
    bool m_partial = false;
    QString m_savedPart;
    QString m_savedDir;
    QString m_savedName;
    QString m_savedKind;
    double m_savedTotal = 0;
    QString m_finalPath;
    bool m_seeding = false;
    double m_servedBytes = 0;
    QString m_errorCode, m_errorTitle, m_errorText, m_errorWay;
    bool m_retryable = false;
    QString m_passwordError;
    bool m_declined = false;
    bool m_wasReceiving = false;
};
