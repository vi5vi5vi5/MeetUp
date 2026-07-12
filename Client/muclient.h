#ifndef MUCLIENT_H
#define MUCLIENT_H

#include <QMainWindow>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MUClient;
}
QT_END_NAMESPACE

class MUClient : public QMainWindow
{
    Q_OBJECT

public:
    MUClient(QWidget *parent = nullptr);
    ~MUClient();

    void SetRegPage();
    void SetAuthPage();
    void SetHomePage();
private:
    Ui::MUClient *ui;

    QHBoxLayout* layout;
    QWidget* authForm;
    QLabel* appName;
    QPushButton* switchThemeBtn;
};

#endif
