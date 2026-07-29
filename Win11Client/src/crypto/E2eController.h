#pragma once
#include <QObject>
#include <QString>

class E2eCipher;
class SignalingClient;
class LinkController;
class SysBridge;

// Ключ комнаты: откуда берётся, когда пропадает и что об этом знает интерфейс.
// Сама криптография — в E2eCipher; здесь только решения. В QML — Crypto.
//
// Ключ НЕ сохраняется на диск — намеренно, как и у веб-клиента (там
// sessionStorage, а не localStorage). Записанная в реестр фраза пережила бы и
// конференцию, и владельца компьютера, а смысл сквозного шифрования ровно в
// обратном. Цена — фразу вводят заново после перезапуска; ссылка с ключом
// (#k=…) этот случай и закрывает.
class E2eController : public QObject {
    Q_OBJECT
    // Шифрование включено: ключ есть, медиа и чат уходят запечатанными.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // Фраза для поля ввода. Пусто при ключе из ссылки: там фразы и не было.
    Q_PROPERTY(QString phrase READ phrase NOTIFY changed)
    // Ключ пришёл ссылкой-приглашением, а не введён фразой.
    Q_PROPERTY(bool fromLink READ fromLink NOTIFY changed)
    // Идёт вывод ключа: PBKDF2 — это 150 000 итераций, десятые доли секунды.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    E2eController(E2eCipher* cipher, SignalingClient* conf, LinkController* link,
                  SysBridge* sys, QObject* parent = nullptr);

    bool active() const { return m_active; }
    QString phrase() const { return m_phrase; }
    bool fromLink() const { return m_fromLink; }
    bool busy() const { return m_busy; }

    // Включить шифрование фразой. Пустая фраза = выключить.
    Q_INVOKABLE void applyPhrase(const QString& phrase);
    Q_INVOKABLE void disable();
    // Ссылка-приглашение с ключом во фрагменте: «…/conference.html?room=КОД#k=…».
    // Фрагмент на сервер не уходит — это свойство адресов, а не наша уловка.
    Q_INVOKABLE QString inviteWithKey() const;

signals:
    void changed();
    void busyChanged();

private:
    void setBusy(bool v);
    void applyKey(const QByteArray& key, const QString& phrase, bool fromLink);
    void onJoinOk();     // вошли в комнату: ключ из ссылки, если он там был
    void onLeft();       // вышли: ключ комнаты больше не наш

    E2eCipher* m_cipher;        // не владеем: он общий на все потоки медиа
    SignalingClient* m_conf;    // не владеем: у него код комнаты (соль ключа)
    LinkController* m_link;     // не владеем: у него ключ из ссылки
    SysBridge* m_sys;           // не владеем: он собирает адрес комнаты

    bool m_active = false;
    bool m_fromLink = false;
    bool m_busy = false;
    QString m_phrase;
    // Номер попытки: пока считается PBKDF2, человек может передумать и ввести
    // другую фразу. Без этого счётчика опоздавший результат затирал бы новый.
    int m_seq = 0;
};
