#pragma once
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

class MURegForm: public QWidget
{
    Q_OBJECT;
public:
    MURegForm(QWidget* parent);
    ~MURegForm();


    QVBoxLayout* vLayout;

    QLabel* image;
    QLabel* loginSign;
    QLineEdit* loginInput;

    QLabel* nameSign;
    QLineEdit* nameInput;

    QLabel* pwd1Sign;
    QLineEdit* pwd1Input;
    QLabel* pwdHint;

    QLabel* pwd2Sign;
    QLineEdit* pwd2Input;

    QPushButton* crAcButton;
    QPushButton* loginButton;

    QLabel* hint;
};
