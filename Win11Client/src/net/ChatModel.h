#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <functional>

// Лента чата. НОВЕЙШЕЕ СООБЩЕНИЕ — В ИНДЕКСЕ 0.
//
// Порядок перевёрнут не из прихоти. Лента в QML показывается с
// `verticalLayoutDirection: BottomToTop`, то есть индекс 0 рисуется внизу
// экрана — там, где ему и место. Выигрыш в том, что «держаться новейшего»
// перестаёт быть вычислением и становится положением: новые строки приходят
// со стороны, к которой лента и так прижата, и ListView удерживает её сам.
//
// Чем это лучше прямого порядка. У сообщений высота своя у каждого (перенос
// текста, картинки), и QQuickListView знает настоящую высоту только тех
// делегатов, которые создал, — остальные оценивает по среднему. Значит
// contentHeight это ОЦЕНКА, и всё, что считается поверх неё («я внизу?»,
// «доехать до низа»), врёт тем сильнее, чем длиннее переписка. Стенд на
// сорока сообщениях разной высоты валил такой подход в 4 сценариях из 9;
// перевёрнутая лента проходит все 9, не имея ни строчки кода прокрутки.
//
// Здесь же и причина, по которой это вообще модель, а не QVariantList: список
// подменялся целиком, ListView на подмену сбрасывает прокрутку, и поверх
// сброса невозможно отличить «человек отлистал вверх почитать» от «модель
// дёрнулась сама». Теперь строки вставляются, а перечитывание новым ключом
// шлёт dataChanged — ни то, ни другое прокрутку не трогает.
class ChatModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        AuthorRole = Qt::UserRole + 1,
        TextRole, TimeRole, SelfRole, LockedRole,
        ImageRole, ImageLockedRole, ImageDroppedRole,
        ImageWidthRole, ImageHeightRole,
    };

    struct Row {
        QString author;
        QString time;
        bool self = false;

        // Как пришло с сервера. Держим, потому что ключ могут ввести уже
        // после сообщения — тогда лента перечитывается этими строками.
        QString raw;
        QString rawImage;
        bool imageDropped = false;   // картинка была, но вытеснена из истории

        // Что показываем текущим ключом.
        QString text;
        QString image;               // image://chatimg/<id> либо пусто
        bool locked = false;
        bool imageLocked = false;
        // Размеры картинки в пикселях. Нужны, чтобы QML отвёл ей место СРАЗУ,
        // не дожидаясь загрузки: иначе пузырь вставляется нулевой высоты и
        // прыгает, когда картинка приезжает.
        int imageW = 0;
        int imageH = 0;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset();                    // другая комната
    // История одним сбросом: строки уже в порядке ленты (новейшее первым).
    // Пятьсот отдельных вставок дали бы пятьсот перекладок впустую.
    void setAll(QList<Row> rows);
    // Живое сообщение. Новейшее — в начало, см. пояснение к классу.
    void prepend(const Row& row);
    // Перечитать все строки (сменился ключ): render правит строку на месте,
    // модель шлёт dataChanged — без сброса и, значит, без прыжка ленты.
    void reRead(const std::function<void(Row&)>& render);

    bool isEmpty() const { return m_rows.isEmpty(); }

private:
    QList<Row> m_rows;
};
