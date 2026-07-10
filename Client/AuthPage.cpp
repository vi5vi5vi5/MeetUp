#include "muclient.h"
#include <QVBoxLayout>

MUAuthForm::MUAuthForm(QWidget* parent): QWidget(parent)
{
    QVBoxLayout* formLayout = new QVBoxLayout(this);
    this->setLayout(formLayout);

    loginSign = new QLabel("ЛОГИН ИЛИ EMAIL", this);
    loginSign->setStyleSheet
        (
            "QLabel {"
            "   margin-left: 50px;"
            "}"
        );
    formLayout->addWidget(loginSign, 0, Qt::AlignmentFlag::AlignLeft);

    loginInput = new QLineEdit(this);
    loginInput->setPlaceholderText("anna@meetup.dev");
    loginInput->setFixedSize(175, 35);

    formLayout->addWidget(loginInput, 0, Qt::AlignmentFlag::AlignCenter);

    pwdSign = new QLabel("ПАРОЛЬ", this);
    pwdSign->setStyleSheet
        (
            "QLabel {"
            "   margin-left: 50px;"
            "}"
            );
    formLayout->addWidget(pwdSign, 0, Qt::AlignmentFlag::AlignLeft);

    pwdInput = new QLineEdit(this);
    pwdInput->setFixedSize(175, 35);
    pwdInput->setEchoMode(QLineEdit::Password);
    pwdInput->setPlaceholderText("••••••••");
    formLayout->addWidget(pwdInput, 0, Qt::AlignmentFlag::AlignCenter);

    loginButton = new QPushButton("Войти", this);
    loginButton->setFixedSize(175, 35);
    formLayout->addWidget(loginButton, 0, Qt::AlignmentFlag::AlignCenter);

    restorePwdButton = new QPushButton("Забыли пароль?", this);
    restorePwdButton->setStyleSheet(
        "QPushButton {"
        "   background: transparent;" // Полностью прозрачный фон
        "   border: none;"             // Убираем серую рамку вокруг текста
        "   color: #ffffff;"           // Белый цвет текста (замените на ваш, если нужен другой)
        "   margin-right: 55px;"
        "}"
        "QPushButton:hover {"
        "   color: #aaaaaa;"
        "}"
        );

    formLayout->addWidget(restorePwdButton, 0, Qt::AlignmentFlag::AlignRight);

    createAccountButton = new QPushButton("Создать аккаунт", this);
    createAccountButton->setFixedSize(175, 35);
    formLayout->addWidget(createAccountButton, 0, Qt::AlignmentFlag::AlignCenter);

    orSign = new QLabel("ИЛИ", this);
    formLayout->addWidget(orSign, 0, Qt::AlignmentFlag::AlignCenter);

    nologinButton = new QPushButton("Войти без аккаунта", this);
    nologinButton->setFixedSize(175, 35);
    formLayout->addWidget(nologinButton, 0, Qt::AlignmentFlag::AlignCenter);

    hint = new QLabel("Аккаунт хранит ваше имя\n       между встречами.", this);
    formLayout->addWidget(hint, 0, Qt::AlignmentFlag::AlignCenter);
}
MUAuthForm::~MUAuthForm()
{
    delete loginSign;
    delete loginInput;
    delete pwdSign;
    delete pwdInput;
    delete loginButton;
    delete restorePwdButton;
    delete createAccountButton;
    delete orSign;
    delete nologinButton;
    delete hint;
}

MUAuthPage::MUAuthPage(QWidget *parent): QWidget(parent)
{
    QHBoxLayout* layout = new QHBoxLayout(parent);
    QVBoxLayout* vLayout = new QVBoxLayout(parent);
    vLayout->setSpacing(10);

    appName = new QLabel("MEETUP");
    appName->setFixedHeight(35);
    layout->addWidget(appName, 0, Qt::AlignmentFlag::AlignTop);

    layout->addStretch();

    QImage img = QImage(300, 100, QImage::Format_RGBA8888); //<-Заглушка
    image = new QLabel();
    image->setPixmap(QPixmap::fromImage(img));
    image->setContentsMargins(0, 100, 0, 0);

    vLayout->addWidget(image, 0, Qt::AlignmentFlag::AlignTop);

    authForm = new MUAuthForm(this);
    vLayout->addWidget(authForm, 0, Qt::AlignmentFlag::AlignTop);
    vLayout->addStretch();

    layout->addLayout(vLayout);
    layout->addStretch();

    switchThemeBtn = new QPushButton();
    switchThemeBtn->setFixedSize(35, 35);
    layout->addWidget(switchThemeBtn, 0, Qt::AlignmentFlag::AlignTop);
}

MUAuthPage::~MUAuthPage()
{
    delete appName;
    delete switchThemeBtn;
    delete image;
    delete authForm;
}
