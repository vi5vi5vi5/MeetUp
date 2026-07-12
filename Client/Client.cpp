#include "muclient.h"
#include "./ui_muclient.h"
#include "RegisterForm.h"
#include "HomePage.h"
#include "AuthForm.h"
#include <QVBoxLayout>

MUClient::MUClient(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MUClient)
{

    ui->setupUi(this);
    layout = new QHBoxLayout(centralWidget());


    appName = new QLabel("MEETUP");
    appName->setFixedHeight(35);
    layout->addWidget(appName, 0, Qt::AlignmentFlag::AlignTop);

    layout->addStretch();

    authForm = new MUAuthForm(this);
    // authForm = new MUMeetingForm(this);

    layout->insertWidget(2, authForm);
    layout->addStretch();

    switchThemeBtn = new QPushButton();
    switchThemeBtn->setFixedSize(35, 35);
    layout->addWidget(switchThemeBtn, 0, Qt::AlignmentFlag::AlignTop);
}

MUClient::~MUClient()
{
    delete appName;
    delete switchThemeBtn;
    if (authForm) delete authForm;
    delete ui;
}

void MUClient::SetRegPage()
{
    delete authForm;
    authForm = new MURegForm(this);
    layout->insertWidget(2, authForm);
}
void MUClient::SetAuthPage()
{
    delete authForm;
    authForm = new MUAuthForm(this);
    layout->insertWidget(2, authForm);
}

void MUClient::SetHomePage()
{
    delete authForm;
    authForm = new MUHomePage(this);
    layout->insertWidget(2, authForm);
}
