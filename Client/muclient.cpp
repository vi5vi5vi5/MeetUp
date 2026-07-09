#include "muclient.h"
#include "./ui_muclient.h"

MUClient::MUClient(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MUClient)
{
    ui->setupUi(this);
}

MUClient::~MUClient()
{
    delete ui;
}
