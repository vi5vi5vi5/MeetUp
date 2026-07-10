#include "muclient.h"
#include "./ui_muclient.h"
#include <QVBoxLayout>

MUClient::MUClient(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MUClient)
{
    ui->setupUi(this);


    authPage = new MUAuthPage(this->centralWidget());
}

MUClient::~MUClient()
{
    delete authPage;
    delete ui;
}
