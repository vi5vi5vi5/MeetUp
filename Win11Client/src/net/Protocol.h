#pragma once
#include <QByteArray>
#include <QtEndian>

// Бинарный медиапротокол MeetUp v2 (методичка сервера, §5).
// Всё числовое — little-endian. Отправляем заголовок 11 байт; принимаем 15 —
// сервер вставил [sender:4] после type и больше ничего не трогал.
// Эти константы — контракт КЛИЕНТОВ: сервер содержимое кадров не разбирает.
namespace Proto {

    // Типы сообщений (type)
    enum : quint8 {
        VIDEO_JPEG = 1,   // legacy: JPEG-кадр камеры
        AUDIO_PCM = 2,   // legacy: PCM 16 кГц Int16 моно 
        VIDEO_CODED = 3,   // кадр видеокодека камеры 
        AUDIO_CODED = 4,   // пакет аудиокодека 
        SCREEN_CODED = 5,   // демонстрация экрана 
        KEYFRAME_REQ = 6,   // «пришлите опорный кадр», payload пуст 
        SCREEN_JPEG = 7,   // legacy: JPEG экрана
    };

    // Флаги (flags) — битовая маска
    enum : quint8 {
        FLAG_KEYFRAME = 1, // опорный кадр (видео, M3)
        FLAG_ENCRYPTED = 2, // payload зашифрован E2E (M5) — до M5 такие кадры дропаем
    };

    // Идентификаторы кодеков (codec)
    enum : quint8 {
        CODEC_VP8 = 1,
        CODEC_H264 = 2,     // поток Annex B! (важно с M3)
        CODEC_VP9 = 3,
        CODEC_OPUS = 4,
    };

    // Кадр, ПРИШЕДШИЙ от сервера (15-байтовый заголовок).
    struct FrameV2 {
        quint8  type = 0;
        quint32 sender = 0;  // id отправителя — вставил сервер
        quint8  flags = 0;
        quint8  codec = 0;
        quint64 ts = 0;      // мс эпохи (стенные часы отправителя!)
        QByteArray payload;
    };

    // Собрать кадр НА ОТПРАВКУ(11 - байтовый заголовок, sender'а у нас нет).
    inline QByteArray pack(quint8 type, quint8 flags, quint8 codec,
            quint64 tsMs, const QByteArray & payload)
    {
        QByteArray out;
        out.reserve(11 + payload.size());
        out.append(char(type));
        out.append(char(flags));
        out.append(char(codec));
        quint64 tsLe = qToLittleEndian(tsMs);
        out.append(reinterpret_cast<const char*>(&tsLe), 8);
        out.append(payload);
        return out;
    }

    // Разобрать кадр от сервера. false — короче заголовка (мусор, выбросить).
    inline bool unpack(const QByteArray& d, FrameV2& f)
    {
        if (d.size() < 15) return false;
        f.type = quint8(d[0]);
        f.sender = qFromLittleEndian<quint32>(d.constData() + 1);
        f.flags = quint8(d[5]);
        f.codec = quint8(d[6]);
        f.ts = qFromLittleEndian<quint64>(d.constData() + 7);
        f.payload = d.mid(15);
        return true;
    }

} // namespace Proto
