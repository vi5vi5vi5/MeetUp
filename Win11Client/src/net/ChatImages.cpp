#include "ChatImages.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>

QString ChatImages::put(const QByteArray& jpeg, QSize* size) {
    if (size) {
        // Только заголовок: QImageReader::size() не разворачивает пиксели, и
        // на два десятка картинок истории это разница между миллисекундами и
        // сотнями миллисекунд на GUI-потоке.
        QBuffer buf;
        buf.setData(jpeg);
        buf.open(QIODevice::ReadOnly);
        *size = QImageReader(&buf, "JPEG").size();
    }
    QMutexLocker lock(&m_lock);
    const QString id = QString::number(++m_seq);
    m_jpeg.insert(id, jpeg);
    return id;
}

void ChatImages::clear() {
    QMutexLocker lock(&m_lock);
    m_jpeg.clear();
}

QImage ChatImages::requestImage(const QString& id, QSize* size, const QSize& requested) {
    QByteArray jpeg;
    {
        QMutexLocker lock(&m_lock);
        jpeg = m_jpeg.value(id);
    }
    if (jpeg.isEmpty()) return {};

    QImage img;
    if (!img.loadFromData(jpeg, "JPEG")) return {};
    if (size) *size = img.size();

    // Размер запрошен — отдаём уменьшенную копию, а не оригинал: иначе каждая
    // картинка в ленте держала бы полный кадр в пиксельном кеше QML.
    if (requested.width() > 0 || requested.height() > 0) {
        const QSize want(requested.width()  > 0 ? requested.width()  : img.width(),
                         requested.height() > 0 ? requested.height() : img.height());
        if (want.width() < img.width() || want.height() < img.height())
            img = img.scaled(want, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img;
}

QByteArray packChatImage(const QImage& src) {
    if (src.isNull()) return {};

    // Прозрачность на белый фон: JPEG альфы не знает, и без подложки
    // прозрачные места станут чёрными (веб делает то же самое).
    QImage flat = src;
    if (flat.hasAlphaChannel()) {
        QImage opaque(flat.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, flat);
        p.end();
        flat = opaque;
    } else if (flat.format() != QImage::Format_RGB32) {
        flat = flat.convertToFormat(QImage::Format_RGB32);
    }

    constexpr int kTargetB64 = 480000;
    const int side = qMax(flat.width(), flat.height());

    QSize triedSize;
    for (int maxDim : { 1600, 1024, 720 }) {
        const QImage step = (side > maxDim)
            ? flat.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : flat;
        // Картинка мельче ступени — кадр тот же, что и на прошлом круге, а все
        // три качества по нему уже перебраны.
        if (step.size() == triedSize) continue;
        triedSize = step.size();
        for (int quality : { 85, 70, 55 }) {
            QByteArray raw;
            QBuffer buf(&raw);
            buf.open(QIODevice::WriteOnly);
            QImageWriter w(&buf, "JPEG");
            w.setQuality(quality);
            if (!w.write(step)) return {};
            buf.close();
            // Считаем длину base64 не переводя: 4 символа на каждые 3 байта.
            if ((raw.size() + 2) / 3 * 4 <= kTargetB64)
                return raw.toBase64();
        }
    }
    return {};
}
