#include "E2eController.h"
#include "E2eCipher.h"
#include "../net/SignalingClient.h"
#include "../net/LinkController.h"
#include "../SysBridge.h"
#include <QThreadPool>
#include <QPointer>

E2eController::E2eController(E2eCipher* cipher, SignalingClient* conf,
                             LinkController* link, SysBridge* sys, QObject* parent)
    : QObject(parent), m_cipher(cipher), m_conf(conf), m_link(link), m_sys(sys) {
    connect(conf, &SignalingClient::joinOk, this, &E2eController::onJoinOk);
    connect(conf, &SignalingClient::left, this, &E2eController::onLeft);
}

void E2eController::setBusy(bool v) {
    if (m_busy == v) return;
    m_busy = v;
    emit busyChanged();
}

void E2eController::applyKey(const QByteArray& key, const QString& phrase, bool fromLink) {
    m_cipher->setKey(key);
    m_active = m_cipher->active();
    m_phrase = m_active ? phrase : QString();
    m_fromLink = m_active && fromLink;
    // Уже показанные сообщения перечитываем новым ключом: фразу вводят и
    // посреди разговора, и переписка до этого момента обязана открыться.
    m_conf->reReadMessages();
    emit changed();
}

// Фразу превращает в ключ PBKDF2 — 150 000 итераций, и на GUI-потоке это
// заметная пауза прямо в момент нажатия. Считаем в пуле, а результат
// возвращаем сюда: трогать ключ имеет право только этот поток.
void E2eController::applyPhrase(const QString& phrase) {
    const QString p = phrase.trimmed();
    const int seq = ++m_seq;
    if (p.isEmpty()) { setBusy(false); applyKey({}, {}, false); return; }

    const QString room = m_conf->roomCode();
    setBusy(true);
    QPointer<E2eController> self(this);
    QThreadPool::globalInstance()->start([self, seq, p, room]() {
        const QByteArray key = E2eCipher::deriveKey(p, room);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, seq, p, key]() {
            if (!self || seq != self->m_seq) return;   // человек уже ввёл другую
            self->setBusy(false);
            self->applyKey(key, p, false);
            }, Qt::QueuedConnection);
        });
}

void E2eController::disable() {
    ++m_seq;                      // отменяем незавершённый вывод ключа
    setBusy(false);
    applyKey({}, {}, false);
}

QString E2eController::inviteWithKey() const {
    const QByteArray key = m_cipher->key();
    const QString code = m_conf->roomCode();
    if (key.isEmpty() || code.isEmpty()) return {};
    return m_sys->roomLink(code) + "#k=" + E2eCipher::keyToBase64Url(key);
}

// Вход в комнату (включая реконнект). Ключ из ссылки применяем здесь, а не
// раньше: до join_ok мы ещё не знаем, в ту ли комнату попали.
void E2eController::onJoinOk() {
    if (m_active) return;                      // ключ уже введён — не перебиваем
    const QString fromLink = m_link->pendingKey();
    if (fromLink.isEmpty()) return;
    const QByteArray key = E2eCipher::keyFromBase64Url(fromLink);
    if (key.isEmpty()) return;                 // битый ключ в ссылке — просто без E2E
    applyKey(key, {}, true);
}

// Ключ выведен под КОД КОМНАТЫ (он в соли), да и держать его дольше разговора
// незачем: следующая комната — другой ключ.
void E2eController::onLeft() {
    ++m_seq;
    setBusy(false);
    applyKey({}, {}, false);
}
