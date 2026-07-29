#pragma once
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

// Картинки чата: хранилище байтов + провайдер для QML.
//
// Хранятся именно JPEG-байты, а не готовые QImage. Сервер держит в истории до
// 24 картинок (ConferenceRoom::kMaxHistoryImages); развёрнутые в пиксели, они
// заняли бы под 180 МБ, в исходном виде — около десяти. Поэтому декодируем в
// requestImage() и сразу под запрошенный размер: пузырь в чате шириной 260 px
// не должен держать в памяти снимок 1600×1200.
//
// requestImage зовут не из GUI-потока, поэтому доступ к таблице под мьютексом.
class ChatImages : public QQuickImageProvider {
public:
    ChatImages() : QQuickImageProvider(QQuickImageProvider::Image) {}

    // Положить картинку и получить id для ссылки image://chatimg/<id>.
    // Каждый вызов даёт НОВЫЙ id: когда человек вводит ключ и лента
    // перечитывается, у картинки обязан смениться URL — иначе QML отдаст из
    // своего кеша прежнюю (то есть ничего, ведь до ключа её и не было).
    QString put(const QByteArray& jpeg);
    // Забыть всё (выход из комнаты, новая история из join_ok).
    void clear();

    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;

private:
    QMutex m_lock;
    QHash<QString, QByteArray> m_jpeg;
    quint64 m_seq = 0;
};

// Ужать картинку под потолок сервера. Возвращает base64 JPEG или пустую строку,
// если не влезло даже в самом мелком варианте.
//
// Лестница повторяет веб (conference.html, fileToJpegB64): сторона
// 1600/1024/720 × качество 85/70/55 до первого попадания. Цель — 480 000
// символов при потолке сервера 600 000: запас нужен, потому что шифрование
// добавляет к строке ещё немного, а отказ приходит уже с сервера, когда
// человек думает, что отправил.
QByteArray packChatImage(const QImage& src);
