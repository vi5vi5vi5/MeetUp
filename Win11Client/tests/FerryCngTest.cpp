// Шифрование Ferry на CNG против ответов OpenSSL.
//
// Самотесты ядра Ferry (ferry-core-test) проверяют шифрование «туда и
// обратно» и HKDF по RFC 5869, но туда-обратно сходится и у неправильной
// реализации, если она неправильна одинаково в обе стороны. Здесь — ответы,
// снятые с OpenSSL (node:crypto) на фиксированных входах: ключ K = 00..1f,
// префикс nonce DEADBEEF, Argon2id с параметрами протокола. Совпали —
// значит, том, зашифрованный CLI или браузером, откроется здесь, и наоборот.
//
// Сборка и запуск:
//   cmake --build out/build/release --target ferry-cng-test
//   out/build/release/ferry-cng-test.exe
#include <cstdio>
#include <string>
#include <vector>

#include "Cli/Password.h"
#include "core/Crypto.h"
#include "core/Protocol.h"

namespace {

int g_failures = 0;

std::string hex(const uint8_t *p, size_t n)
{
    static const char *d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}
std::string hex(const ferry::Bytes &b) { return hex(b.data(), b.size()); }
std::string hex(const ferry::Key32 &k) { return hex(k.data(), k.size()); }

void check(bool ok, const char *what)
{
    std::printf("  %s  %s\n", ok ? "ok    " : "ПРОВАЛ", what);
    if (!ok)
        ++g_failures;
}

void expectHex(const std::string &got, const char *want, const char *what)
{
    const bool ok = got == want;
    check(ok, what);
    if (!ok)
        std::printf("          ждали %s\n          вышло %s\n", want, got.c_str());
}

std::vector<uint8_t> seq(size_t n, int mod)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = uint8_t(i % size_t(mod));
    return v;
}

} // namespace

int main()
{
    ferry::Key32 master{};
    for (size_t i = 0; i < master.size(); ++i)
        master[i] = uint8_t(i);
    const ferry::TransferKeys keys = ferry::TransferKeys::derive(master);
    const uint8_t prefix[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    std::printf("\nВывод ключей (HKDF-SHA256)\n");
    expectHex(hex(keys.data), "510165a8abb2e753674201ed7bebea0ecd02207c5de7d9f67083c168b0059d1d", "K_data");
    expectHex(hex(keys.meta), "791e98ed2bd9bd917264147b49f386512a500e524cafe831b839b9cd49c79728", "K_meta");
    expectHex(hex(keys.verifier), "c8213a00307a76aa447d5f0d99bbedc6cd6f4d45974410ad8cf3505e75e6398a", "V");

    std::printf("\nAES-256-GCM\n");
    const std::vector<uint8_t> plain = seq(1000, 251);
    ferry::Bytes chunk;
    check(ferry::sealChunk(keys.data, prefix, 5, 100, plain.data(), plain.size(), chunk),
          "чанк зашифрован");
    expectHex(hex(chunk.data(), 16), "76ee0b7999fd8aa1163cde5fdc089d1b", "начало шифротекста");
    expectHex(chunk.size() >= 16 ? hex(chunk.data() + chunk.size() - 16, 16) : std::string(),
              "d2684dfb463732c7c9430f52fbab9a03", "тег GCM");
    ferry::Bytes back;
    check(ferry::openChunk(keys.data, prefix, 5, 100, chunk.data(), chunk.size(), back)
              && back == ferry::Bytes(plain.begin(), plain.end()),
          "открылся обратно");

    // Манифест: ключ K_meta, «индекс» 2^64-1 — проверка, что метка целиком
    // уходит в nonce и AAD, а не обрезается до 32 бит.
    const std::string manifest = "{\"name\":\"\xD1\x82\xD0\xB5\xD1\x81\xD1\x82.txt\",\"total\":1000}";
    ferry::Bytes sealed;
    ferry::sealChunk(keys.meta, prefix, ferry::kMetaLabelManifest, 1,
                     reinterpret_cast<const uint8_t *>(manifest.data()), manifest.size(), sealed);
    expectHex(hex(sealed),
              "cdaed33e5f0601b7627f26adccc8873b10df5355477e8154e1c560adb30ba1389730ba02176370685307cab3278da9a9dcbee3dd",
              "манифест под меткой 2^64-1");

    ferry::Bytes empty;
    check(ferry::sealChunk(keys.data, prefix, 0, 0, nullptr, 0, empty), "пустой текст зашифрован");
    expectHex(hex(empty), "aaeee9490ef075212118da33a012990a", "тег пустого текста");
    check(ferry::openChunk(keys.data, prefix, 0, 0, empty.data(), empty.size(), back) && back.empty(),
          "пустой текст открылся");

    ferry::Bytes damaged = chunk;
    damaged[3] ^= 0x40;
    back = {1, 2, 3};
    check(!ferry::openChunk(keys.data, prefix, 5, 100, damaged.data(), damaged.size(), back)
              && back == ferry::Bytes({1, 2, 3}),
          "испорченный не открылся и не тронул выход");

    std::printf("\nДоказательство владения ключом\n");
    std::vector<uint8_t> challenge(16);
    for (size_t i = 0; i < challenge.size(); ++i)
        challenge[i] = uint8_t(i + 1);
    expectHex(hex(ferry::proveKeyOwnership(keys.verifier, challenge.data(), challenge.size())),
              "3683efcbac36403df0d97c8a3a8e4f3ba4a505131fa5172dc0954c471f2f26b0", "HMAC(V, challenge)");

    std::printf("\nПароль (Argon2id, t=3, m=64 МиБ, p=1)\n");
    // Та же фраза, что в самотестах ядра, набранная «как попало»: регистр,
    // «ё», неразрывный пробел посередине и пробелы по краям.
    const std::string typed = "  \xD0\x9C\xD0\xBE\xD0\xB9 \xD1\x81\xD0\xB5\xD1\x80\xD1\x8B\xD0\xB9"
                              "\xC2\xA0\xD1\x91\xD0\xB6 \xD0\xB5\xD1\x81\xD1\x82 "
                              "\xD0\xB9\xD0\xBE\xD0\xB3\xD1\x83\xD1\x80\xD1\x82 "
                              "\xD1\x83\xD1\x82\xD1\x80\xD0\xBE\xD0\xBC ";
    const std::string normalized = ferry::cli::normalizePassword(typed);
    check(ferry::cli::passwordProblem(normalized).empty(), "фраза годится");
    const std::vector<uint8_t> salt = seq(16, 256);
    ferry::Key32 fromPassword{};
    check(ferry::cli::passwordKey(normalized, salt.data(), salt.size(), fromPassword), "ключ выведен");
    expectHex(hex(fromPassword), "1fd7625fb8dfd6bb5f363416f3e16576a624e20c7ab452f9199bd7282d32b94b",
              "Argon2id");

    bool ok = false;
    const ferry::Key32 a = ferry::randomKey32(&ok);
    const ferry::Key32 b = ferry::randomKey32();
    check(ok && a != b, "CSPRNG даёт разные ключи");

    std::printf("\n%s\n", g_failures ? "ЕСТЬ РАСХОЖДЕНИЯ" : "Всё сошлось с OpenSSL");
    return g_failures ? 1 : 0;
}
