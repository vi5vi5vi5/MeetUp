#pragma once
#include <QObject>

class ApiClient;

// Мост к системным мелочам, которых нет в QML: буфер обмена, абсолютные
// ссылки-приглашения и адрес сервера (его знает только ApiClient). В QML — Sys.
class SysBridge : public QObject {
    Q_OBJECT
    // Не CONSTANT: адрес можно сменить на экране входа — ссылки пересчитаются.
    Q_PROPERTY(QString host READ host NOTIFY serverChanged)
    Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY serverChanged)
public:
    explicit SysBridge(ApiClient* api, QObject* parent = nullptr);

    QString host() const;                                    // "meetup.linkpc.net"
    QString serverAddress() const;                           // как показывать в поле ввода
    Q_INVOKABLE void copyText(const QString& text);          // в буфер обмена
    Q_INVOKABLE QString roomLink(const QString& code) const; // полная ссылка на комнату
    // Сменить сервер: "meetup.linkpc.net" -> https://…; "http://host" — как есть;
    // пусто — вернуть прод по умолчанию. Сохраняется между запусками.
    Q_INVOKABLE void setServer(const QString& input);

signals:
    void serverChanged();

private:
    ApiClient* m_api;   // не владеем
};
