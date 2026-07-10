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



class MUAuthForm: public QWidget
{
    Q_OBJECT

public:
    MUAuthForm(QWidget *parent);
    ~MUAuthForm();

    QLabel* loginSign;
    QLineEdit* loginInput;

    QLabel* pwdSign;
    QLineEdit* pwdInput;

    QPushButton* loginButton;
    QPushButton* restorePwdButton;
    QPushButton* createAccountButton;

    QLabel* orSign;
    QPushButton* nologinButton;
    QLabel* hint;
};


class MUClient : public QMainWindow
{
    Q_OBJECT

public:
    MUClient(QWidget *parent = nullptr);
    ~MUClient();

private:
    Ui::MUClient *ui;

    MUAuthForm* authForm;
    QLabel* appName;
    QLabel* image;
    QPushButton* switchThemeBtn;
};

#endif
