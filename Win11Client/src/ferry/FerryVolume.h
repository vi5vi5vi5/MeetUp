#pragma once

// Том Ferry глазами получателя: что сказал релей о раздаче и что вышло,
// когда это открыли ключом из ссылки. Без сети и без диска — только разбор
// и проверки, поэтому годится и для карточки в ленте (показать имя и размер
// до «Принять»), и для самого приёма.
//
// Эталон — Server/web/assets/js/volume.js и api.js в репозитории Ferry:
// порядок проверок тот же, коды отказов тоже — они уходят в карточку и
// должны значить одно и то же у веба, CLI и здесь.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

#include "core/Chunker.h"
#include "core/Crypto.h"
#include "core/Manifest.h"
#include "core/VolumeLayout.h"

class QNetworkReply;
class QNetworkRequest;
class QUrl;

// Ответ GET /api/transfers/<id>. Эта ручка ничего не сжигает — ни
// использования, ни challenge, — поэтому по ней можно ходить сколько угодно.
struct FerryMeta
{
    QString id;
    quint64 total = 0;
    quint32 chunkSize = 0;
    quint64 chunks = 0;
    QByteArray manifest;      // шифротекст манифеста
    QByteArray hashList;      // шифротекст списка хешей; пусто при хешах на лету
    QByteArray noncePrefix;   // 4 байта
    QByteArray salt;          // 16 байт, только у раздачи по паролю
    bool password = false;    // mode == "password"
    QString state;            // active | delegated | closed
    int receivers = 0;
    qint64 usesLeft = -1;     // -1 — без лимита
    qint64 expiresInMs = 0;
    bool requireApproval = false;
    bool stream = false;      // хеши ещё считаются и едут сегментами
    quint64 hashed = 0;

    static bool parse(const QByteArray &json, FerryMeta &out);
};

namespace FerryApi {

// Запрос к релею. Без кук (у Ferry их нет, а чужие слать незачем), без
// перехода по переадресациям (API Ferry их не делает: переадресация здесь —
// это уже не тот, кого спрашивали) и с таймаутом.
QNetworkRequest request(const QString &origin, const QString &path);

// Что ответил релей: код из {"error": "..."}, иначе http_<статус>, а нет
// ответа вовсе — "network".
QString errorCode(QNetworkReply *reply, const QByteArray &body);

// Код отказа -> что сказать человеку и что ему делать. Тексты — с доски
// «Ошибки» макета Ferry, чтобы все клиенты говорили одно.
struct Refusal
{
    QString title;    // что случилось
    QString text;     // почему
    QString way;      // что делать
};
Refusal describe(const QString &code);

// После каких кодов переподключаться бессмысленно: ответ не изменится.
bool isFinal(const QString &code);

} // namespace FerryApi

// Том, открытый ключом. Всё, что нужно и карточке, и приёму.
struct FerryVolume
{
    ferry::TransferKeys keys;
    ferry::ChunkPlan plan;
    ferry::Manifest manifest;
    ferry::VolumeLayout layout;
    std::vector<uint8_t> hashes;   // count * 32; пусто, пока хеши едут
    quint64 files = 0;             // для карточки: сколько файлов
    quint64 dirs = 0;              // и пустых папок

    // false — code получает код отказа (bad_meta, bad_key, bad_manifest,
    // bad_hashes), text — объяснение для человека.
    static bool open(const FerryMeta &meta, const ferry::Key32 &key, FerryVolume &out,
                     QString *code, QString *text);
};

// Тот же ли это том: промежуточный манифест (без корня) и итоговый обязаны
// совпасть во всём, кроме корня, — иначе итоговый подменили.
bool ferrySameVolume(const ferry::Manifest &a, const ferry::Manifest &b);

// Ключ из ссылки Ferry, «голого» ключа (43 знака base64url, можно с #) или
// ссылки целиком, вставленных человеком в поле. false — ничего похожего.
bool ferryKeyFromText(const QString &text, ferry::Key32 &out);
