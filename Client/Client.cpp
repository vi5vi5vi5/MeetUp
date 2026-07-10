#include "muclient.h"
#include "./ui_muclient.h"
#include <QVBoxLayout>

MUClient::MUClient(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MUClient)
{
    ui->setupUi(this);
    QHBoxLayout* layout = new QHBoxLayout(centralWidget());
    QVBoxLayout* vLayout = new QVBoxLayout(centralWidget());
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

MUClient::~MUClient()
{
    delete appName;
    delete switchThemeBtn;
    delete image;
    if (authForm) delete authForm;
    delete ui;
}
