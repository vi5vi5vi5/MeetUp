#pragma once
#include <QByteArray>
#include <QString>
#include <QMutex>
#include <atomic>
#include <memory>

// Сквозное шифрование (M5). Формат — байт в байт как у веб-клиента
// (`Server/web/assets/meetup-media.js`): люди сидят в одной комнате из браузера
// и из этого приложения, и расхождение на один байт означает не «хуже
// совместимость», а «друг друга не слышно».
//
//   ключ    AES-256, из фразы через PBKDF2-HMAC-SHA256, 150 000 итераций,
//           соль "meetup-e2e-v1|<код комнаты>" — либо готовые 32 байта из
//           ссылки-приглашения (#k=<base64url>);
//   payload [iv:12][шифротекст][тег:16], где iv = [префикс сессии:4][счётчик:8 LE];
//   AAD     [type, codec] — два байта заголовка кадра. Заголовок остаётся
//           открытым: сервер обязан читать тип, чтобы релеить полосы.
//
// Считает Windows CNG (bcrypt): AES-GCM и PBKDF2 есть в самой системе, а
// тащить ради них OpenSSL в дистрибутив клиента, который и так весь построен
// на Windows-API, незачем.
//
// Объект ОДИН на приложение и живёт на всех потоках сразу: шифруют звук
// (аудиопоток), камера и экран (два потока кодирования), расшифровывают два
// потока декодирования, а чат — GUI. Поэтому счётчик атомарный: один общий
// префикс и сквозная нумерация надёжнее, чем свой счётчик у каждого потока —
// два одинаковых iv на одном ключе разрушают GCM полностью.
class E2eCipher {
public:
    // «Типы» для AAD нетекстовых полос — как у веба (там же и объяснение,
    // почему у чата отдельные: иначе шифротекст чата можно было бы подсунуть
    // в видеополосу и наоборот).
    enum : quint8 { ChatAad = 250, ImageAad = 251 };

    E2eCipher();
    ~E2eCipher();

    // ---- ключи (потокобезопасно) ----
    // Фраза -> 32 байта. Пустая фраза даёт пустой массив (шифрование выключено).
    static QByteArray deriveKey(const QString& phrase, const QString& roomCode);
    static QByteArray keyFromBase64Url(const QString& b64);   // пусто, если не 32 байта
    static QString    keyToBase64Url(const QByteArray& key);
    static QByteArray randomKey();

    // Пустой ключ = шифрование выключено. Смена ключа сбрасывает счётчик и
    // берёт новый префикс: нумерация уникальна в пределах КЛЮЧА.
    void setKey(const QByteArray& key);
    bool active() const { return m_active.load(std::memory_order_relaxed); }
    QByteArray key() const;

    // ---- медиа ----
    // Пусто на выходе = не смогли (нет ключа, чужой ключ, порченые данные).
    // Молчание честнее каши: у вызывающего это значит «кадр не наш».
    QByteArray seal(quint8 type, quint8 codec, const QByteArray& plain) const;
    QByteArray open(quint8 type, quint8 codec, const QByteArray& sealed) const;

    // ---- чат ----
    // Текст уезжает строкой "🔒e2e:<base64url(iv|ct|tag)>" — сервер хранит и
    // релеит её как обычное сообщение, ничего не зная о шифровании.
    static bool isSealedText(const QString& s);
    QString sealText(const QString& text) const;
    // Возвращает false, если расшифровать не удалось (нет ключа или чужой).
    bool openText(const QString& sealed, QString& outText) const;

    // Картинка чата. Конверт на проводе тот же — "🔒e2e:<base64url>", — но
    // AAD-тип свой, и шифруются САМИ БАЙТЫ JPEG, а не их base64-запись. Веб
    // делает ровно так (sealImage переводит b64 в байты перед seal), и разойтись
    // здесь нельзя: картинка просто не откроется на другой стороне.
    QString sealImage(const QByteArray& jpeg) const;
    bool openImage(const QString& sealed, QByteArray& outJpeg) const;

private:
    struct Key;                                  // держит хендл CNG и сами байты
    std::shared_ptr<const Key> currentKey() const;

    mutable QMutex m_lock;                       // защищает только СМЕНУ ключа
    std::shared_ptr<const Key> m_key;
    std::atomic<bool> m_active{ false };
    mutable std::atomic<quint64> m_counter{ 0 };
    // Атомик, а не просто поле: пишется при смене ключа, читается шифрующими
    // потоками без мьютекса — на паре кадров в момент смены это была бы гонка.
    std::atomic<quint32> m_prefix{ 0 };          // меняется вместе с ключом
};
