// Шифрование Ferry на Windows CNG — реализация core/Crypto.h из ядра Ferry.
//
// У самого Ferry эти функции живут на OpenSSL (core/Crypto.cpp, цель
// ferry-crypto). Клиент MeetUp OpenSSL не возит вовсе: сквозное шифрование
// конференции уже сделано на CNG (см. src/crypto), и тащить ~5 МБ в
// дистрибутив ради тех же AES-GCM и HMAC, что есть в самой Windows, незачем.
//
// Совместимость здесь — это совпадение байт с OpenSSL, браузером и CLI
// Ferry: расхождение в одном байте означает «том не открывается», а не
// «немного хуже». Поэтому проверка не на глаз, а двумя наборами:
//   * самотесты ядра Ferry (tests/CoreTests.cpp), собранные против этого
//     файла, — цель ferry-core-test;
//   * ответы, снятые с OpenSSL на фиксированных входах, — ferry-cng-test.
// Обе цели не входят в обычную сборку, см. CMakeLists.txt.
#include "core/Crypto.h"

#include <algorithm>
#include <climits>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#ifndef STATUS_SUCCESS
#  define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace ferry {
namespace {

// Провайдеры открываются один раз на процесс: это дескрипторы алгоритма без
// состояния, и BCrypt* с ними потокобезопасен — чанки шифруются в нескольких
// потоках сразу.
struct Providers
{
    BCRYPT_ALG_HANDLE aesGcm = nullptr;
    BCRYPT_ALG_HANDLE hmacSha256 = nullptr;

    Providers()
    {
        if (BCryptOpenAlgorithmProvider(&aesGcm, BCRYPT_AES_ALGORITHM, nullptr, 0) != STATUS_SUCCESS) {
            aesGcm = nullptr;
        } else if (BCryptSetProperty(aesGcm, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                     sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != STATUS_SUCCESS) {
            BCryptCloseAlgorithmProvider(aesGcm, 0);
            aesGcm = nullptr;
        }
        if (BCryptOpenAlgorithmProvider(&hmacSha256, BCRYPT_SHA256_ALGORITHM, nullptr,
                                        BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS)
            hmacSha256 = nullptr;
    }
};

Providers &providers()
{
    static Providers p;   // ленивая и потокобезопасная инициализация
    return p;
}

// Ключ AES живёт ровно один вызов. Держать хендлы между чанками можно было бы
// ради скорости, но расписание ключа AES — микросекунды против миллисекунд на
// чанк, а кэш ключей — это ещё одно место, где ключ задерживается в памяти.
struct AesKey
{
    BCRYPT_KEY_HANDLE handle = nullptr;

    explicit AesKey(const Key32 &raw)
    {
        BCRYPT_ALG_HANDLE alg = providers().aesGcm;
        if (alg && BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0,
                                              const_cast<PUCHAR>(raw.data()), ULONG(raw.size()),
                                              0) != STATUS_SUCCESS)
            handle = nullptr;
    }
    ~AesKey()
    {
        if (handle)
            BCryptDestroyKey(handle);
    }
    AesKey(const AesKey &) = delete;
    AesKey &operator=(const AesKey &) = delete;
};

void putBe64(uint8_t *out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(v >> (56 - 8 * i));
}

// nonce = [ префикс раздачи : 4 ][ индекс : 8 BE ]
void buildNonce(const uint8_t noncePrefix[kNoncePrefixSize], uint64_t index, uint8_t out[12])
{
    std::memcpy(out, noncePrefix, kNoncePrefixSize);
    putBe64(out + kNoncePrefixSize, index);
}

// AAD = [ версия : 1 ][ индекс : 8 BE ][ всего чанков : 8 BE ]
void buildAad(uint64_t index, uint64_t totalChunks, uint8_t out[17])
{
    out[0] = kProtocolVersion;
    putBe64(out + 1, index);
    putBe64(out + 9, totalChunks);
}

} // namespace

bool randomBytes(uint8_t *out, size_t len)
{
    while (len > 0) {
        const ULONG part = ULONG(std::min<size_t>(len, ULONG_MAX));
        if (BCryptGenRandom(nullptr, out, part, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != STATUS_SUCCESS)
            return false;
        out += part;
        len -= part;
    }
    return true;
}

bool randomBytes(Bytes &out, size_t len)
{
    out.assign(len, 0);
    return randomBytes(out.data(), len);
}

Key32 randomKey32(bool *ok)
{
    Key32 k{};
    const bool good = randomBytes(k.data(), k.size());
    if (ok)
        *ok = good;
    if (!good)
        k.fill(0);
    return k;
}

Bytes hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen)
{
    BCRYPT_ALG_HANDLE alg = providers().hmacSha256;
    if (!alg || keyLen > ULONG_MAX || msgLen > ULONG_MAX)
        return {};
    // BCryptHash (Windows 10+) — HMAC за один вызов: ключ HMAC идёт секретом.
    static const uint8_t kDummy = 0;
    Bytes out(32, 0);
    if (BCryptHash(alg, const_cast<PUCHAR>(keyLen ? key : &kDummy), ULONG(keyLen),
                   const_cast<PUCHAR>(msgLen ? msg : &kDummy), ULONG(msgLen),
                   out.data(), ULONG(out.size())) != STATUS_SUCCESS)
        return {};
    return out;
}

// HKDF-SHA256 (RFC 5869) на HMAC — так же, как у Ferry на OpenSSL. В CNG есть
// и свой BCRYPT_HKDF_ALGORITHM (с Windows 10 1709), но эталон Ferry — HKDF
// поверх HMAC, и повторить его буква в букву проще, чем доказывать
// равенство двух разных реализаций.
Bytes hkdfSha256(const uint8_t *ikm, size_t ikmLen,
                 const uint8_t *salt, size_t saltLen,
                 const std::string &info, size_t outLen)
{
    if (outLen == 0 || outLen > 255 * 32)
        return {};

    // Extract. Пустая соль по RFC — блок нулей длиной с хеш.
    static const uint8_t kZeroSalt[32] = {};
    const uint8_t *s = (salt && saltLen) ? salt : kZeroSalt;
    const size_t sLen = (salt && saltLen) ? saltLen : sizeof(kZeroSalt);
    const Bytes prk = hmacSha256(s, sLen, ikm, ikmLen);
    if (prk.size() != 32)
        return {};

    // Expand.
    Bytes out;
    out.reserve(outLen);
    Bytes t;
    uint8_t counter = 1;
    while (out.size() < outLen) {
        Bytes block;
        block.reserve(t.size() + info.size() + 1);
        block.insert(block.end(), t.begin(), t.end());
        block.insert(block.end(), info.begin(), info.end());
        block.push_back(counter);

        t = hmacSha256(prk.data(), prk.size(), block.data(), block.size());
        if (t.size() != 32)
            return {};

        const size_t take = std::min(t.size(), outLen - out.size());
        out.insert(out.end(), t.begin(), t.begin() + static_cast<long>(take));
        ++counter;
    }
    return out;
}

TransferKeys TransferKeys::derive(const Key32 &master)
{
    TransferKeys keys;
    const auto one = [&](const char *info, Key32 &dst) {
        const Bytes b = hkdfSha256(master.data(), master.size(), nullptr, 0, info, 32);
        if (b.size() == 32)
            std::memcpy(dst.data(), b.data(), 32);
    };
    one(kInfoData, keys.data);
    one(kInfoMeta, keys.meta);
    one(kInfoVerifier, keys.verifier);
    return keys;
}

Bytes proveKeyOwnership(const Key32 &verifier, const uint8_t *challenge, size_t len)
{
    return hmacSha256(verifier.data(), verifier.size(), challenge, len);
}

bool sealChunk(const Key32 &key, const uint8_t noncePrefix[kNoncePrefixSize],
               uint64_t index, uint64_t totalChunks,
               const uint8_t *plain, size_t plainLen, Bytes &out)
{
    if (plainLen > ULONG_MAX - kGcmTagSize)
        return false;
    AesKey k(key);
    if (!k.handle)
        return false;

    uint8_t nonce[12];
    uint8_t aad[17];
    buildNonce(noncePrefix, index, nonce);
    buildAad(index, totalChunks, aad);

    Bytes sealed(plainLen + kGcmTagSize, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce;
    info.cbNonce = sizeof(nonce);
    info.pbAuthData = aad;
    info.cbAuthData = sizeof(aad);
    info.pbTag = sealed.data() + plainLen;
    info.cbTag = ULONG(kGcmTagSize);

    // Пустой открытый текст законен (тег над одним AAD), но указатель CNG
    // всё равно нужен ненулевой.
    uint8_t dummy = 0;
    ULONG written = 0;
    if (BCryptEncrypt(k.handle, const_cast<PUCHAR>(plainLen ? plain : &dummy), ULONG(plainLen),
                      &info, nullptr, 0, plainLen ? sealed.data() : &dummy, ULONG(plainLen),
                      &written, 0) != STATUS_SUCCESS
        || written != plainLen)
        return false;
    out = std::move(sealed);
    return true;
}

bool openChunk(const Key32 &key, const uint8_t noncePrefix[kNoncePrefixSize],
               uint64_t index, uint64_t totalChunks,
               const uint8_t *cipher, size_t cipherLen, Bytes &out)
{
    if (cipherLen < kGcmTagSize || cipherLen > ULONG_MAX)
        return false;
    const size_t bodyLen = cipherLen - kGcmTagSize;
    AesKey k(key);
    if (!k.handle)
        return false;

    uint8_t nonce[12];
    uint8_t aad[17];
    uint8_t tag[kGcmTagSize];
    buildNonce(noncePrefix, index, nonce);
    buildAad(index, totalChunks, aad);
    std::memcpy(tag, cipher + bodyLen, kGcmTagSize);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = nonce;
    info.cbNonce = sizeof(nonce);
    info.pbAuthData = aad;
    info.cbAuthData = sizeof(aad);
    info.pbTag = tag;
    info.cbTag = sizeof(tag);

    Bytes plain(bodyLen, 0);
    uint8_t dummy = 0;
    ULONG written = 0;
    const NTSTATUS st = BCryptDecrypt(k.handle, const_cast<PUCHAR>(bodyLen ? cipher : &dummy),
                                      ULONG(bodyLen), &info, nullptr, 0,
                                      bodyLen ? plain.data() : &dummy, ULONG(bodyLen),
                                      &written, 0);
    if (st != STATUS_SUCCESS || written != bodyLen) {
        // Тег не сошёлся (STATUS_AUTH_TAG_MISMATCH) или CNG отказал. Открытый
        // текст наружу не отдаём вовсе — он не аутентифицирован. CNG и сам
        // обнуляет буфер при несошедшемся теге, но полагаться на это незачем.
        SecureZeroMemory(plain.data(), plain.size());
        return false;
    }
    out = std::move(plain);
    return true;
}

} // namespace ferry
