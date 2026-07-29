#include "E2eCipher.h"
#include <QtEndian>
#include <QDebug>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#ifndef STATUS_SUCCESS
#  define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

static const int  kKeyBytes = 32;                       // AES-256
static const int  kIvBytes = 12;                        // GCM: 96 бит — канон
static const int  kTagBytes = 16;
static const quint64 kIterations = 150000;              // столько же у веба
static const char kSaltPrefix[] = "meetup-e2e-v1|";

// Маркер шифрованного текста чата — "🔒e2e:". Байты эмодзи заданы escape'ами, а
// не буквой в исходнике: это единственная строка, где ошибка кодировки
// компилятора не видна глазом, но молча ломает совместимость с браузером.
// В UTF-16 (а QString именно такой) 🔒 — суррогатная пара, поэтому маркер
// занимает ШЕСТЬ позиций, а не пять; у веба ровно та же арифметика (slice(6)).
static const int kTextMarkChars = 6;
static QString textMark() {
    static const QString m = QString::fromUtf8("\xF0\x9F\x94\x92" "e2e:");
    return m;
}

// Провайдеры CNG открываются один раз на процесс: это просто дескрипторы
// алгоритма, состояния в них нет, и BCrypt* с ними потокобезопасен.
namespace {

struct Providers {
    BCRYPT_ALG_HANDLE aes = nullptr;
    BCRYPT_ALG_HANDLE hmacSha256 = nullptr;

    Providers() {
        if (BCryptOpenAlgorithmProvider(&aes, BCRYPT_AES_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
            qWarning() << "E2eCipher: CNG не дал AES";
            aes = nullptr;
        } else if (BCryptSetProperty(aes, BCRYPT_CHAINING_MODE,
                                     (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                     sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != STATUS_SUCCESS) {
            qWarning() << "E2eCipher: CNG не дал режим GCM";
            BCryptCloseAlgorithmProvider(aes, 0);
            aes = nullptr;
        }
        if (BCryptOpenAlgorithmProvider(&hmacSha256, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS) {
            qWarning() << "E2eCipher: CNG не дал HMAC-SHA256";
            hmacSha256 = nullptr;
        }
    }
};

Providers& providers() {
    static Providers p;      // ленивая и потокобезопасная инициализация (C++11)
    return p;
}

} // namespace

// Ключ живёт как неизменяемый объект под shared_ptr: пока идёт шифрование
// кадра, пользователь может ввести другую фразу, и хендл не должен исчезнуть
// из-под работающего потока. Мьютекс защищает только подмену указателя.
struct E2eCipher::Key {
    BCRYPT_KEY_HANDLE handle = nullptr;
    QByteArray raw;                      // для экспорта в ссылку-приглашение

    ~Key() { if (handle) BCryptDestroyKey(handle); }
};

E2eCipher::E2eCipher() {}
E2eCipher::~E2eCipher() {}

// ---------- ключи ----------

QByteArray E2eCipher::deriveKey(const QString& phrase, const QString& roomCode) {
    if (phrase.isEmpty()) return {};
    BCRYPT_ALG_HANDLE alg = providers().hmacSha256;
    if (!alg) return {};

    const QByteArray pass = phrase.toUtf8();
    const QByteArray salt = QByteArray(kSaltPrefix) + roomCode.toUtf8();

    QByteArray out(kKeyBytes, 0);
    const NTSTATUS st = BCryptDeriveKeyPBKDF2(
        alg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(pass.constData())), ULONG(pass.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(salt.constData())), ULONG(salt.size()),
        kIterations,
        reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()), 0);
    if (st != STATUS_SUCCESS) {
        qWarning() << "E2eCipher: PBKDF2 =" << Qt::hex << quint32(st);
        return {};
    }
    return out;
}

QByteArray E2eCipher::keyFromBase64Url(const QString& b64) {
    const QByteArray raw = QByteArray::fromBase64(
        b64.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return raw.size() == kKeyBytes ? raw : QByteArray();
}

QString E2eCipher::keyToBase64Url(const QByteArray& key) {
    if (key.size() != kKeyBytes) return {};
    return QString::fromLatin1(key.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray E2eCipher::randomKey() {
    QByteArray out(kKeyBytes, 0);
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()), ULONG(out.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != STATUS_SUCCESS)
        return {};
    return out;
}

void E2eCipher::setKey(const QByteArray& key) {
    std::shared_ptr<const Key> fresh;

    if (key.size() == kKeyBytes && providers().aes) {
        auto k = std::make_shared<Key>();
        k->raw = key;
        const NTSTATUS st = BCryptGenerateSymmetricKey(
            providers().aes, &k->handle, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
            ULONG(key.size()), 0);
        if (st != STATUS_SUCCESS)
            qWarning() << "E2eCipher: BCryptGenerateSymmetricKey =" << Qt::hex << quint32(st);
        else
            fresh = k;
    }

    QMutexLocker lock(&m_lock);
    m_key = fresh;
    // Нумерация уникальна в пределах ключа, поэтому и префикс, и счётчик
    // начинаются заново вместе с ним.
    quint32 prefix = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&prefix), sizeof(prefix),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    m_prefix.store(prefix, std::memory_order_relaxed);
    m_counter.store(0, std::memory_order_relaxed);
    m_active.store(fresh != nullptr, std::memory_order_relaxed);
}

QByteArray E2eCipher::key() const {
    const auto k = currentKey();
    return k ? k->raw : QByteArray();
}

std::shared_ptr<const E2eCipher::Key> E2eCipher::currentKey() const {
    QMutexLocker lock(&m_lock);
    return m_key;
}

// ---------- медиа ----------

QByteArray E2eCipher::seal(quint8 type, quint8 codec, const QByteArray& plain) const {
    const auto k = currentKey();
    if (!k) return {};

    QByteArray out(kIvBytes + plain.size() + kTagBytes, 0);
    uchar* iv = reinterpret_cast<uchar*>(out.data());
    qToLittleEndian<quint32>(m_prefix.load(std::memory_order_relaxed), iv);
    // Счётчик атомарный: полосы шифруют с разных потоков, а повтор iv на одном
    // ключе — это не «немного слабее», это раскрытие обоих кадров сразу.
    qToLittleEndian<quint64>(m_counter.fetch_add(1, std::memory_order_relaxed), iv + 4);

    uchar aad[2] = { type, codec };
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = iv;
    info.cbNonce = kIvBytes;
    info.pbAuthData = aad;
    info.cbAuthData = sizeof(aad);
    info.pbTag = reinterpret_cast<PUCHAR>(out.data()) + kIvBytes + plain.size();
    info.cbTag = kTagBytes;

    ULONG done = 0;
    const NTSTATUS st = BCryptEncrypt(
        k->handle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plain.constData())), ULONG(plain.size()),
        &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(out.data()) + kIvBytes, ULONG(plain.size()), &done, 0);
    if (st != STATUS_SUCCESS || done != ULONG(plain.size())) {
        qWarning() << "E2eCipher: BCryptEncrypt =" << Qt::hex << quint32(st);
        return {};
    }
    return out;
}

QByteArray E2eCipher::open(quint8 type, quint8 codec, const QByteArray& sealed) const {
    const auto k = currentKey();
    if (!k) return {};
    if (sealed.size() < kIvBytes + kTagBytes) return {};

    const int ctSize = sealed.size() - kIvBytes - kTagBytes;
    uchar aad[2] = { type, codec };
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(sealed.constData()));
    info.cbNonce = kIvBytes;
    info.pbAuthData = aad;
    info.cbAuthData = sizeof(aad);
    info.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(sealed.constData()))
                 + kIvBytes + ctSize;
    info.cbTag = kTagBytes;

    QByteArray out(ctSize, 0);
    ULONG done = 0;
    // Чужой ключ и порченый кадр приходят сюда одинаково — как несовпадение
    // тега. Это не ошибка программы, поэтому молча и без warning: на каждый
    // кадр чужого потока в лог сыпалось бы по строке.
    const NTSTATUS st = BCryptDecrypt(
        k->handle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(sealed.constData())) + kIvBytes,
        ULONG(ctSize),
        &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(out.data()), ULONG(ctSize), &done, 0);
    if (st != STATUS_SUCCESS || done != ULONG(ctSize)) return {};
    return out;
}

// ---------- чат ----------

bool E2eCipher::isSealedText(const QString& s) { return s.startsWith(textMark()); }

QString E2eCipher::sealText(const QString& text) const {
    const QByteArray body = seal(ChatAad, 0, text.toUtf8());
    if (body.isEmpty()) return {};
    return textMark() + QString::fromLatin1(body.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool E2eCipher::openText(const QString& sealed, QString& outText) const {
    if (!isSealedText(sealed)) return false;
    const QByteArray body = QByteArray::fromBase64(
        sealed.mid(kTextMarkChars).toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray plain = open(ChatAad, 0, body);
    if (plain.isEmpty()) return false;
    outText = QString::fromUtf8(plain);
    return true;
}

QString E2eCipher::sealImage(const QByteArray& jpeg) const {
    const QByteArray body = seal(ImageAad, 0, jpeg);
    if (body.isEmpty()) return {};
    return textMark() + QString::fromLatin1(body.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool E2eCipher::openImage(const QString& sealed, QByteArray& outJpeg) const {
    if (!isSealedText(sealed)) return false;
    const QByteArray body = QByteArray::fromBase64(
        sealed.mid(kTextMarkChars).toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QByteArray plain = open(ImageAad, 0, body);
    if (plain.isEmpty()) return false;
    outJpeg = plain;
    return true;
}
