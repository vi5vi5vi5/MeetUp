#include "RegisterForm.h"
#include "muclient.h"
MURegForm::MURegForm(QWidget* parent): QWidget(parent)
{
    vLayout = new QVBoxLayout(this);
    vLayout->setSpacing(5);
    this->setLayout(vLayout);

    QImage img = QImage(300, 100, QImage::Format_RGBA8888); //<-Заглушка
    image = new QLabel();
    image->setPixmap(QPixmap::fromImage(img));
    image->setContentsMargins(0, 75, 0, 0);
    vLayout->addWidget(image, 0, Qt::AlignmentFlag::AlignTop);


    loginSign = new QLabel("ЛОГИН", this);
    loginSign->setStyleSheet
    (
        "QLabel {"
        "   margin-top: 5px;"
        "   margin-left: 60px;"
        "}"
    );
    vLayout->addWidget(loginSign, 0, Qt::AlignmentFlag::AlignLeft);

    loginInput = new QLineEdit(this);
    loginInput->setPlaceholderText("anna");
    loginInput->setFixedSize(175, 35);
    vLayout->addWidget(loginInput, 0, Qt::AlignmentFlag::AlignCenter);

    nameSign = new QLabel("ОТОБРАЖАЕМОЕ ИМЯ", this);
    nameSign->setStyleSheet
    (
        "QLabel {"
        "   margin-left: 60px;"
        "}"
    );
    vLayout->addWidget(nameSign, 0, Qt::AlignmentFlag::AlignLeft);

    nameInput = new QLineEdit(this);
    nameInput->setPlaceholderText("Анна");
    nameInput->setFixedSize(175, 35);
    vLayout->addWidget(nameInput, 0, Qt::AlignmentFlag::AlignCenter);

    pwd1Sign = new QLabel("ПАРОЛЬ", this);
    pwd1Sign->setStyleSheet
    (
        "QLabel {"
        "   margin-left: 60px;"
        "}"
    );
    vLayout->addWidget(pwd1Sign, 0, Qt::AlignmentFlag::AlignLeft);

    pwd1Input = new QLineEdit(this);
    pwd1Input->setPlaceholderText("••••••••");
    pwd1Input->setFixedSize(175, 35);
    vLayout->addWidget(pwd1Input, 0, Qt::AlignmentFlag::AlignCenter);

    pwdHint = new QLabel("Минимум 8 символов", this);
    vLayout->addWidget(pwdHint, 0, Qt::AlignmentFlag::AlignCenter);

    pwd2Sign = new QLabel("ПОВТОРИТЕ ПАРОЛЬ", this);
    pwd2Sign->setStyleSheet
    (
        "QLabel {"
        "   margin-left: 60px;"
        "}"
    );
    vLayout->addWidget(pwd2Sign, 0, Qt::AlignmentFlag::AlignLeft);

    pwd2Input = new QLineEdit(this);
    pwd2Input->setPlaceholderText("••••••••");
    pwd2Input->setFixedSize(175, 35);
    vLayout->addWidget(pwd2Input, 0, Qt::AlignmentFlag::AlignCenter);

    crAcButton = new QPushButton("Создать аккаунт", this);
    crAcButton->setFixedSize(175, 35);
    vLayout->addWidget(crAcButton, 0, Qt::AlignmentFlag::AlignCenter);

    loginButton = new QPushButton("Войти", this);
    loginButton->setStyleSheet
    (
        "QPushButton"
        "{"
        "   background: transparent;" // Полностью прозрачный фон
        "   border: none;"             // Убираем серую рамку вокруг текста
        "   color: #ffffff;"           // Белый цвет текста (замените на ваш, если нужен другой)
        "   margin-right: 65px;"
        "}"
        "QPushButton:hover"
        "{"
        "   color: #aaaaaa;"
        "}"
    );
    vLayout->addWidget(loginButton, 0, Qt::AlignmentFlag::AlignRight);

    hint = new QLabel("Пароль хранится только в виде хэша", this);
    vLayout->addWidget(hint, 0, Qt::AlignmentFlag::AlignCenter);

    vLayout->addStretch();




    connect(crAcButton, &QPushButton::clicked, parent, [=]
    {
        QString login = loginInput->text();
        QString password1 = pwd1Input->text();
        QString password2 = pwd2Input->text();

        if (password1 == password2) if (MUClient* cli = dynamic_cast<MUClient*>(parent)) cli->SetHomePage();
    });
    connect(loginButton, &QPushButton::clicked, parent, [=]
    {
        if (MUClient* cli = dynamic_cast<MUClient*>(parent))
            cli->SetAuthPage();
    });
}

MURegForm::~MURegForm()
{
    delete vLayout;
    delete image;
    delete loginSign;
    delete loginInput;

    delete nameSign;
    delete nameInput;

    delete pwd1Sign;
    delete pwd1Input;
    delete pwdHint;

    delete pwd2Sign;
    delete pwd2Input;

    delete crAcButton;
    delete loginButton;

    delete hint;
}
