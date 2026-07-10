#include "AuthForm.h"
#include "muclient.h"

MUAuthForm::MUAuthForm(QWidget* parent): QWidget(parent)
{
    formLayout = new QVBoxLayout(this);
    formLayout->setSpacing(5);
    this->setLayout(formLayout);


    QImage img = QImage(300, 100, QImage::Format_RGBA8888); //<-Заглушка
    image = new QLabel();
    image->setPixmap(QPixmap::fromImage(img));
    image->setContentsMargins(0, 75, 0, 0);
    formLayout->addWidget(image, 0, Qt::AlignmentFlag::AlignTop);

    loginSign = new QLabel("ЛОГИН ИЛИ EMAIL", this);
    loginSign->setStyleSheet
    (
        "QLabel {"
        "   margin-top: 5px;"
        "   margin-left: 60px;"
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
        "   margin-left: 60px;"
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
    restorePwdButton->setStyleSheet
    (
        "QPushButton {"
        "   background: transparent;" // Полностью прозрачный фон
        "   border: none;"             // Убираем серую рамку вокруг текста
        "   color: #ffffff;"           // Белый цвет текста (замените на ваш, если нужен другой)
        "   margin-right: 65px;"
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

    formLayout->addStretch();
    connect(loginButton, &QPushButton::clicked, parent, [=]
    {
        QString login = loginInput->text();
        QString password = pwdInput->text();

        if (true) if (MUClient* cli = dynamic_cast<MUClient*>(parent)) cli->SetHomePage();
    });
    connect(createAccountButton, &QPushButton::clicked, parent, [=]
    {
        if (MUClient* cli = dynamic_cast<MUClient*>(parent)) cli->SetRegPage();
    });
}

MUAuthForm::~MUAuthForm()
{
    delete image;
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
    delete formLayout;
}
