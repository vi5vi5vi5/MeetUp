#ifndef MUCLIENT_H
#define MUCLIENT_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MUClient;
}
QT_END_NAMESPACE

class MUClient : public QMainWindow
{
    Q_OBJECT

public:
    MUClient(QWidget *parent = nullptr);
    ~MUClient();

private:
    Ui::MUClient *ui;
};
#endif // MUCLIENT_H
