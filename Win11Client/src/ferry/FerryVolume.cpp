#include "FerryVolume.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>

#include <cstring>

#include "core/Base64Url.h"
#include "core/HashList.h"
#include "core/Link.h"
#include "core/Protocol.h"

namespace {

// base64url из ответа релея. Строго: релей сам кодирует ровно так, и
// поблажки здесь означали бы принять то, что прислал не он.
bool b64(const QJsonValue &v, QByteArray &out, int wantLen = -1)
{
    if (!v.isString())
        return false;
    const auto r = QByteArray::fromBase64Encoding(
        v.toString().toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals
            | QByteArray::AbortOnBase64DecodingErrors);
    if (!r)
        return false;
    if (wantLen >= 0 && r.decoded.size() != wantLen)
        return false;
    out = r.decoded;
    return true;
}

} // namespace

bool FerryMeta::parse(const QByteArray &json, FerryMeta &out)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.isEmpty())
        return false;
    FerryMeta m;
    m.id = o.value(QStringLiteral("id")).toString();
    const double total = o.value(QStringLiteral("total")).toDouble(-1);
    const double chunkSize = o.value(QStringLiteral("chunk_size")).toDouble(-1);
    const double chunks = o.value(QStringLiteral("chunks")).toDouble(-1);
    // Числа приходят как double: до 2^53 они точны, а дальше тома не бывает.
    if (total < 0 || chunkSize <= 0 || chunks < 0 || total > 9.0e15 || chunkSize > 1 << 30)
        return false;
    m.total = quint64(total);
    m.chunkSize = quint32(chunkSize);
    m.chunks = quint64(chunks);
    m.password = o.value(QStringLiteral("mode")).toString() == QLatin1String("password");
    m.state = o.value(QStringLiteral("state")).toString();
    m.receivers = o.value(QStringLiteral("receivers")).toInt();
    const QJsonValue uses = o.value(QStringLiteral("uses_left"));
    m.usesLeft = uses.isDouble() ? qint64(uses.toDouble()) : -1;
    m.expiresInMs = qint64(o.value(QStringLiteral("expires_in_ms")).toDouble());
    m.requireApproval = o.value(QStringLiteral("require_approval")).toBool();
    m.stream = o.value(QStringLiteral("hash_mode")).toString() == QLatin1String("stream");
    m.hashed = quint64(qMax(0.0, o.value(QStringLiteral("hashed")).toDouble()));
    if (!b64(o.value(QStringLiteral("manifest")), m.manifest)
        || !b64(o.value(QStringLiteral("nonce_prefix")), m.noncePrefix, int(ferry::kNoncePrefixSize)))
        return false;
    if (!m.stream && !b64(o.value(QStringLiteral("hash_list")), m.hashList))
        return false;
    if (m.password && !b64(o.value(QStringLiteral("salt")), m.salt, int(ferry::kPasswordSaltSize)))
        return false;
    out = std::move(m);
    return true;
}

namespace FerryApi {

QNetworkRequest request(const QString &origin, const QString &path)
{
    QNetworkRequest req(QUrl(origin + path));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(15000);
    return req;
}

QString errorCode(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0)
        return QStringLiteral("network");
    const QString code = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toString();
    if (!code.isEmpty())
        return code;
    return QStringLiteral("http_%1").arg(status);
}

Refusal describe(const QString &code)
{
    // Порядок — по частоте: то, что увидят чаще, выше.
    if (code == QLatin1String("need_key"))
        return {QStringLiteral("Ссылка неполная"),
                QStringLiteral("В ней нет части после #, а это и есть ключ. Использование при этом не списано."),
                QStringLiteral("Скопируйте ссылку целиком, вместе с частью после #")};
    if (code == QLatin1String("sender_gone"))
        return {QStringLiteral("Рейс окончен"),
                QStringLiteral("Взять том неоткуда: отправитель ушёл, и все, кто скачал его целиком, тоже. "
                               "На релее тома нет — так задумано."),
                QStringLiteral("Если том ещё нужен — попросите переотправить")};
    if (code == QLatin1String("transfer_not_found"))
        return {QStringLiteral("Такой раздачи нет"),
                QStringLiteral("Раздачу закрыли, срок вышел или релей перезапускали."),
                QStringLiteral("Нужна новая ссылка от отправителя")};
    if (code == QLatin1String("uses_exhausted"))
        return {QStringLiteral("Лимит использований исчерпан"),
                QStringLiteral("Отправитель выдал том ограниченное число раз, и все они уже потрачены. "
                               "Ваш ключ тут ни при чём."),
                QStringLiteral("Новая ссылка от отправителя")};
    if (code == QLatin1String("expired"))
        return {QStringLiteral("Срок раздачи вышел"), QString(),
                QStringLiteral("Попросите отправителя запустить раздачу заново")};
    if (code == QLatin1String("approval_denied"))
        return {QStringLiteral("Отправитель не пустил"), QStringLiteral("Он решил не выдавать вам том."),
                QStringLiteral("Напишите отправителю напрямую")};
    if (code == QLatin1String("revoked"))
        return {QStringLiteral("Отправитель закрыл доступ"), QString(),
                QStringLiteral("Если том ещё нужен — напишите отправителю")};
    if (code == QLatin1String("too_many_receivers"))
        return {QStringLiteral("Мест нет"),
                QStringLiteral("У раздачи потолок одновременных получателей. Он бережёт канал отправителя, "
                               "а не ограничивает вас лично."),
                QStringLiteral("Попробуйте через несколько минут")};
    if (code == QLatin1String("server_busy"))
        return {QStringLiteral("Релей занят"),
                QStringLiteral("Окна раздач живут в его памяти, и сейчас она кончилась: новым он честно "
                               "отказывает, а не замедляет текущие."),
                QStringLiteral("Попробуйте позже")};
    if (code == QLatin1String("no_source"))
        return {QStringLiteral("Начало тома взять неоткуда"), QString(),
                QStringLiteral("Попробуйте позже или попросите отправителя раздать заново")};
    if (code == QLatin1String("preparing"))
        return {QStringLiteral("Отправитель ещё готовит том"), QString(),
                QStringLiteral("Попробуйте через минуту")};
    if (code == QLatin1String("chunk_mismatch"))
        return {QStringLiteral("Данные не сошлись с контрольной суммой"),
                QStringLiteral("Подряд пришло слишком много испорченных кусков."),
                QStringLiteral("Начните приём заново — лучше ничего, чем тихо испорченный том")};
    if (code == QLatin1String("no_space"))
        return {QStringLiteral("Не хватает места на диске"), QString(),
                QStringLiteral("Освободите место или выберите другую папку в настройках")};
    if (code == QLatin1String("bad_key"))
        return {QStringLiteral("Ключ не подошёл"),
                QStringLiteral("Манифест не расшифровался: ключ в ссылке не тот, либо релей описал том "
                               "иначе, чем отправитель."),
                QStringLiteral("Скопируйте ссылку целиком заново")};
    if (code == QLatin1String("bad_manifest") || code == QLatin1String("bad_hashes")
        || code == QLatin1String("bad_meta"))
        return {QStringLiteral("Том не прошёл проверку"), QString(),
                QStringLiteral("Лучше ничего, чем тихо испорченный том: попросите переотправить")};
    if (code == QLatin1String("io"))
        return {QStringLiteral("Не получилось записать на диск"), QString(),
                QStringLiteral("Проверьте папку для приёма в настройках")};
    if (code == QLatin1String("bad_message") || code == QLatin1String("owner_conflict"))
        return {QStringLiteral("Релей нарушил протокол"), QString(),
                QStringLiteral("Попробуйте ещё раз; повторится — откройте ссылку в браузере")};
    if (code == QLatin1String("network"))
        return {QStringLiteral("Релей не отвечает"), QString(),
                QStringLiteral("Проверьте сеть и попробуйте снова")};
    if (code == QLatin1String("not_ferry"))
        return {QStringLiteral("Это не раздача Ferry"),
                QStringLiteral("Адрес похож, но по нему отвечает что-то другое."),
                QStringLiteral("Откройте ссылку в браузере")};
    return {QStringLiteral("Релей отказал"), QString(), QStringLiteral("Попробуйте позже")};
}

bool isFinal(const QString &code)
{
    static const QSet<QString> kFinal = {
        QStringLiteral("need_key"), QStringLiteral("transfer_not_found"),
        QStringLiteral("uses_exhausted"), QStringLiteral("expired"),
        QStringLiteral("sender_gone"), QStringLiteral("no_source"),
        QStringLiteral("chunk_mismatch"), QStringLiteral("owner_conflict"),
        QStringLiteral("bad_message"), QStringLiteral("approval_denied"),
        QStringLiteral("revoked"), QStringLiteral("bad_hashes"), QStringLiteral("bad_key"),
        QStringLiteral("bad_manifest"), QStringLiteral("bad_meta"), QStringLiteral("not_ferry"),
    };
    return kFinal.contains(code);
}

} // namespace FerryApi

namespace {

// Windows не различает регистр имён: «Отчёт.txt» и «отчёт.txt» из одного
// тома легли бы в один файл, и второй молча затёр бы первый — хеши при этом
// сошлись бы (чанки-то верные). Санитизация путей ядра этого не ловит: на
// POSIX это два разных файла. Отказываем вслух, как и на любой кривой путь.
bool caseCollision(const ferry::Manifest &m, QString *which)
{
    QSet<QString> files;
    QSet<QString> dirs;
    for (const ferry::ManifestEntry &e : m.entries) {
        const QString original = QString::fromStdString(e.path);
        const QString p = original.toCaseFolded();
        for (int pos = p.indexOf(QLatin1Char('/')); pos >= 0; pos = p.indexOf(QLatin1Char('/'), pos + 1)) {
            const QString parent = p.left(pos);
            if (files.contains(parent)) {
                *which = original;
                return true;
            }
            dirs.insert(parent);
        }
        if (files.contains(p) || (!e.isDir && dirs.contains(p))) {
            *which = original;
            return true;
        }
        (e.isDir ? dirs : files).insert(p);
    }
    return false;
}

} // namespace

bool FerryVolume::open(const FerryMeta &meta, const ferry::Key32 &key, FerryVolume &out,
                       QString *code, QString *text)
{
    const auto fail = [&](const char *c, const QString &t) {
        if (code)
            *code = QString::fromLatin1(c);
        if (text)
            *text = t;
        return false;
    };

    FerryVolume v;
    v.plan = ferry::planWith(meta.total, meta.chunkSize);
    if (!v.plan.valid() || v.plan.chunkCount != meta.chunks)
        return fail("bad_meta", QStringLiteral("Релей описал том неправдоподобно — размеры не сходятся."));

    v.keys = ferry::TransferKeys::derive(key);
    const uint8_t *prefix = reinterpret_cast<const uint8_t *>(meta.noncePrefix.constData());

    // Раздача с хешами на лету: манифест промежуточный и лежит под своей
    // меткой, списка пока нет — он приедет сегментами по вебсокету.
    const uint64_t label = meta.stream ? ferry::kMetaLabelStreamManifest : ferry::kMetaLabelManifest;
    ferry::Bytes plain;
    if (!ferry::openChunk(v.keys.meta, prefix, label, v.plan.chunkCount,
                          reinterpret_cast<const uint8_t *>(meta.manifest.constData()),
                          size_t(meta.manifest.size()), plain))
        return fail("bad_key", QStringLiteral("Манифест не расшифровался."));

    std::string err;
    if (!ferry::Manifest::fromJson(std::string(plain.begin(), plain.end()), v.manifest, &err))
        return fail("bad_manifest", QStringLiteral("Манифест не разобрался: %1").arg(QString::fromStdString(err)));
    if (v.manifest.total != meta.total)
        return fail("bad_manifest", QStringLiteral("Размер в манифесте не сходится с размером раздачи."));
    // Релей сказал «хеши на лету», а манифест говорит обратное (или
    // наоборот) — это чужой или подменённый манифест.
    if (v.manifest.streamHashes != meta.stream)
        return fail("bad_manifest", QStringLiteral("Манифест не той формы, какую описал релей."));
    if (ferry::safeFileName(v.manifest.name).empty())
        return fail("bad_manifest", QStringLiteral("Имя тома не годится для файла."));
    QString collision;
    if (v.manifest.isTree() && caseCollision(v.manifest, &collision))
        return fail("bad_manifest",
                    QStringLiteral("В томе есть пути, которые Windows не различает: «%1».").arg(collision));

    if (!meta.stream) {
        ferry::Bytes list;
        if (!ferry::openChunk(v.keys.meta, prefix, ferry::kMetaLabelHashList, v.plan.chunkCount,
                              reinterpret_cast<const uint8_t *>(meta.hashList.constData()),
                              size_t(meta.hashList.size()), list))
            return fail("bad_hashes", QStringLiteral("Список хешей не расшифровался."));
        if (list.size() != v.plan.chunkCount * 32)
            return fail("bad_hashes", QStringLiteral("Список хешей не той длины."));
        // Корень лежит в манифесте: пока он не сошёлся со списком, списку
        // верить нельзя, а без него нельзя проверить ни одного чанка.
        if (ferry::blake3(list.data(), list.size()) != v.manifest.root)
            return fail("bad_hashes", QStringLiteral("Корневой хеш не сошёлся со списком хешей."));
        v.hashes.assign(list.begin(), list.end());
    }

    v.layout.build(v.manifest);
    if (v.manifest.isTree()) {
        for (const ferry::ManifestEntry &e : v.manifest.entries)
            (e.isDir ? v.dirs : v.files) += 1;
    } else {
        v.files = 1;
    }
    out = std::move(v);
    return true;
}

bool ferrySameVolume(const ferry::Manifest &a, const ferry::Manifest &b)
{
    if (a.version != b.version || a.kind != b.kind || a.name != b.name || a.total != b.total
        || a.entries.size() != b.entries.size())
        return false;
    for (size_t i = 0; i < a.entries.size(); ++i) {
        const ferry::ManifestEntry &x = a.entries[i];
        const ferry::ManifestEntry &y = b.entries[i];
        if (x.path != y.path || x.size != y.size || x.mtime != y.mtime || x.isDir != y.isDir)
            return false;
    }
    return true;
}

bool ferryKeyFromText(const QString &text, ferry::Key32 &out)
{
    const QString s = text.trimmed();
    if (s.contains(QLatin1String("/t/"))) {
        ferry::TransferLink link;
        if (!ferry::TransferLink::parse(s.toStdString(), link) || !link.hasKey)
            return false;
        out = link.key;
        return true;
    }
    std::string raw = s.toStdString();
    if (!raw.empty() && raw.front() == '#')
        raw.erase(raw.begin());
    std::vector<uint8_t> bytes;
    if (!ferry::base64UrlDecode(raw, bytes) || bytes.size() != out.size())
        return false;
    std::memcpy(out.data(), bytes.data(), out.size());
    return true;
}
