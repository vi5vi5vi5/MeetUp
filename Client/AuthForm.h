#pragma once

#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

class MUAuthForm: public QWidget
{
    Q_OBJECT

public:
    MUAuthForm(QWidget *parent);
    ~MUAuthForm();

    QVBoxLayout* formLayout;
    QLabel* image;
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
