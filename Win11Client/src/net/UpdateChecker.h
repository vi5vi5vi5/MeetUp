#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Обновление клиента: узнать, скачать, подменить себя, перезапуститься.
// В QML — Updates.
//
// Откуда узнаём: GitHub Releases API того репозитория, из которого собран
// клиент (UPDATE_REPO задаётся в CMake — форк подставит свой). Не свой сервер:
// сервер MeetUp обновляется отдельно от клиента, и его версия отвечает на
// другой вопрос. Побочная выгода — раздачей занимается CDN GitHub, а не
// чей-то домашний сервер.
//
// Как ставим: работающий .exe и занятые .dll на Windows НЕЛЬЗЯ удалить, но
// можно переименовать. На этом всё и держится — старые файлы переезжают в
// «имя.old-метка», новые распаковываются на их место, приложение
// перезапускается и подчищает хвосты при следующем старте (sweepOldFiles).
// Отдельный updater.exe при таком раскладе не нужен вовсе.
//
// Чего здесь намеренно нет: подписи манифеста. Целостность держится на HTTPS
// до api.github.com и до самого файла, на проверке, что адрес скачивания
// действительно ведёт на GitHub (редирект в чужие руки не уведёт), и на
// совпадении размера с заявленным в API.
class UpdateChecker : public QObject {
    Q_OBJECT
    // Для QML — строкой: объект отдан туда через setContextProperty, а тогда
    // имя типа в QML не существует и написать UpdateChecker.Ready нельзя.
    // Сравнивать же с голыми числами — заведомо нечитаемый код.
    Q_PROPERTY(QString stateName READ stateName NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    // 0..100 на время скачивания; -1 — размер неизвестен.
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
public:
    enum State {
        Idle,          // ещё не спрашивали
        Checking,      // спрашиваем GitHub
        // Спросили — свежее нашего ничего нет. Отдельно от Idle: кнопке в
        // «О программе» нужно ответить «у вас последняя версия», а не молчать
        // ровно так же, как до нажатия.
        UpToDate,
        Available,     // вышла новая версия, файл ещё не качали
        Downloading,
        Ready,         // распаковано и готово подменить себя
        Failed
    };
    Q_ENUM(State)

    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    State state() const { return m_state; }
    QString stateName() const;
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString errorText() const { return m_error; }
    int progress() const { return m_progress; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    // Подменить файлы и перезапуститься. args — с чем стартовать заново
    // (см. Cli): адрес сервера, комната, ключ. Возврат false — не срослось,
    // причина в errorText, приложение продолжает работать как ни в чём не бывало.
    Q_INVOKABLE bool installAndRestart(const QStringList& args = {});

    // Прибрать «*.old-*», оставшиеся от прошлого обновления. Зовётся из main()
    // при старте: раньше нельзя — файлы заняты нами же до самого перезапуска.
    static void sweepOldFiles();

    // -1 / 0 / 1. Понимает «1.05», «v1.6.2», хвосты вида «1.06-beta» отбрасывает.
    static int compareVersions(const QString& a, const QString& b);
    // Номер версии из тега релиза («Win11Client_1.05» -> «1.05»).
    static QString versionFromTag(const QString& tag);

signals:
    void changed();
    void progressChanged();

private:
    void setState(State s);
    void fail(const QString& text);
    void onCheckReply(QNetworkReply* reply);
    void onDownloadReply(QNetworkReply* reply);
    // Распаковать скачанный архив. Возвращает папку с готовыми файлами
    // (внутри архива может быть как плоский набор, так и одна папка).
    QString unpack(const QString& zipPath);
    // Можно ли вообще писать туда, где мы лежим. Portable-раздача в
    // %LOCALAPPDATA% — можно; установка в Program Files — нет, и об этом надо
    // сказать словами, а не молча свалиться на середине подмены.
    static bool canWriteToAppDir(QString* why);

    QNetworkAccessManager* m_net;
    QTimer* m_poll;                 // редкая перепроверка: клиент живёт сутками

    State m_state = Idle;
    QString m_latest;               // версия из tag_name, без «v»
    QString m_notes;                // тело релиза, обрезанное до вменяемой длины
    QString m_error;
    QString m_assetUrl;             // прямая ссылка на .zip
    qint64 m_assetSize = 0;
    QString m_unpacked;             // папка с распакованным (в состоянии Ready)
    int m_progress = -1;
};
