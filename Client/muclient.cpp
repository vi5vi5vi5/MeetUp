#include "muclient.h"
#include "./ui_muclient.h"
#include <QVBoxLayout>

MUClient::MUClient(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MUClient)
{
    ui->setupUi(this);


    MUUpperWidgets* uw = new MUUpperWidgets(this->centralWidget());
    QVBoxLayout* vl = new QVBoxLayout(this);
    centralWidget()->setLayout(vl);
}

MUClient::~MUClient()
{
    delete ui;
}


MUAuthPage::MUAuthPage(QWidget *parent): QWidget(parent)
{

}
MUAuthPage::~MUAuthPage() {}


MUAuthForm::MUAuthForm(QWidget* parent): QWidget(parent)
{
    QVBoxLayout* formLayout = new QVBoxLayout(this);
    this->setLayout(formLayout);

    loginSign = new QLabel("ЛОГИН ИЛИ ПОЧТА", this);
    formLayout->addWidget(loginSign);

    loginInput = new QLineEdit(this);
    formLayout->addWidget(loginInput);

    pwdSign = new QLabel("ПАРОЛЬ", this);
    formLayout->addWidget(pwdSign);

    pwdInput = new QLineEdit(this);
    formLayout->addWidget(pwdInput);

    loginButton = new QPushButton("Войти", this);
    formLayout->addWidget(loginButton);

    restorePwdButton = new QPushButton("Забыли пароль?", this);
    formLayout->addWidget(restorePwdButton);

    createAccountButton = new QPushButton("Создать аккаунт", this);
    formLayout->addWidget(createAccountButton);

    orSign = new QLabel("ИЛИ", this);
    formLayout->addWidget(orSign);

    nologinButton = new QPushButton(this);
    formLayout->addWidget(nologinButton);

    hint = new QLabel("Аккаунт хранит ваше имя между встречами", this);
    formLayout->addWidget(hint);
}
MUAuthForm::~MUAuthForm() {}

MUUpperWidgets::MUUpperWidgets(QWidget *parent): QWidget(parent)
{
    QHBoxLayout* layout = new QHBoxLayout(parent);

    appName = new QLabel("MEETUP");
    layout->addWidget(appName, 0, Qt::AlignmentFlag::AlignTop);

    layout->addStretch();

    QImage* img = new QImage(300, 100, QImage::Format_RGBA8888);
    image = new QLabel();
    image->setPixmap(QPixmap::fromImage(*img));
    delete img;
    image->setContentsMargins(0, 100, 0, 0);
    layout->addWidget(image, 0, Qt::AlignTop);

    layout->addStretch();

    switchThemeBtn = new QPushButton();
    layout->addWidget(switchThemeBtn, 0, Qt::AlignmentFlag::AlignTop);
}

MUUpperWidgets::~MUUpperWidgets()
{
    delete appName;
    delete switchThemeBtn;
}
