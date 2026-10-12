#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>

class QWindow;

// Нативная обвязка окон «картинка в картинке» (QML: PipHelper).
//
// Само окно — обычный QML Window без рамки; всё, что умеет только Windows,
// живёт здесь:
//
//   * скругление углов Windows 11 и системная тень — без них окно без рамки
//     выглядит вырезанным ножницами прямоугольником поверх чужого приложения;
//   * пропорции кадра при растягивании (WM_SIZING): окно тянется за край и
//     сразу держит 16:9 или что там у демонстрации, без полос по краям и без
//     догоняющего «доворота» после того, как кнопку отпустили;
//   * магнит к краям рабочей области (WM_MOVING): брошенное у края окно встаёт
//     ровно с отступом, а не в двух пикселях от панели задач;
//   * конец перетаскивания (WM_EXITSIZEMOVE) — QML запоминает место и размер.
//
// Сообщения ловятся подклассом оконной процедуры (SetWindowSubclass), а не
// нативным фильтром Qt: Qt 6 пропускает через фильтры только те сообщения,
// которые знает сам, а WM_SIZING и WM_MOVING среди них нет — проба
// scratchpad/piptest видела WM_ENTERSIZEMOVE и WM_EXITSIZEMOVE в фильтре, но
// ни одного WM_SIZING за всё растягивание.
//
// Перетаскивание и растягивание запускает QML штатными
// QWindow::startSystemMove/startSystemResize: цикл перемещения остаётся
// системным (плавность, курсоры, Esc отменяет), а подкласс лишь правит
// прямоугольник, который этот цикл предлагает.
class PipHelper : public QObject {
    Q_OBJECT
public:
    explicit PipHelper(QObject* parent = nullptr);
    ~PipHelper() override;

    // Взять окно под опеку: скругление, тень, пропорции и магнит. Повторный
    // вызов безвреден (окно могло пересоздать нативный хендл).
    Q_INVOKABLE void attach(QWindow* w);
    Q_INVOKABLE void detach(QWindow* w);
    // Пропорции кадра (ширина/высота); 0 — тянется свободно.
    Q_INVOKABLE void setAspect(QWindow* w, qreal aspect);

    // Рабочая область экрана, на котором стоит окно (без панели задач), в
    // логических пикселях Qt. По ней QML ставит окно в угол при первом показе.
    Q_INVOKABLE QRect workArea(QWindow* w) const;
    // Рабочая область экрана, которому принадлежит точка; пустая, если точка
    // не попадает ни на один экран (монитор отключили с прошлого раза).
    Q_INVOKABLE QRect workAreaAt(int x, int y) const;

    // Вернуть окно на передний план — из свёрнутого в то состояние, в котором
    // его сворачивали (развёрнутое остаётся развёрнутым: showNormal() тут
    // ошибся бы), и сделать активным.
    Q_INVOKABLE void bringToFront(QWindow* w);

signals:
    // Человек отпустил окно после перетаскивания или растягивания.
    void moveFinished(QWindow* window);

private:
    // Оконная процедура-подкласс; data — указатель на PipHelper. Возвращает
    // true, если сообщение обработано и result заполнен.
    bool handleMessage(quintptr hwnd, unsigned msg, quintptr wParam, qintptr lParam,
                       qintptr* result);
    friend struct PipSubclass;

    struct Entry {
        QPointer<QWindow> window;
        qreal aspect = 0;
    };
    // Ключ — HWND: в оконную процедуру приходит голое сообщение Windows.
    QHash<quintptr, Entry> m_windows;
};
