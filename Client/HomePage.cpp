#include "HomePage.h"



MUHomePage::MUHomePage(QWidget* parent): QWidget(parent)
{

    vLayout = new QVBoxLayout(this);
    this->setLayout(vLayout);

    crMeetingButton = new QPushButton("Создайте личную встречу", this);
    crMeetingButton->setFixedSize(190, 185);
    vLayout->addWidget(crMeetingButton, 0, Qt::AlignmentFlag::AlignCenter);

    meetingForm = new MUMeetingForm(this);
    vLayout->addWidget(meetingForm, 0, Qt::AlignmentFlag::AlignCenter);
}
MUHomePage::~MUHomePage()
{
    delete vLayout;
    delete crMeetingButton;
    delete meetingForm;
}

MUMeetingForm::MUMeetingForm(QWidget* parent): QWidget(parent)
{
    vLayout = new QVBoxLayout(this);
    vLayout->setSpacing(5);
    this->setLayout(vLayout);

    header = new QLabel("Другая встреча", this);
    header->setContentsMargins(38, 0, 0, 0);
    vLayout->addWidget(header, 0, Qt::AlignmentFlag::AlignLeft);

    otherRoomSign = new QLabel("КОД ЧУЖОЙ КОМНАТЫ", this);
    otherRoomSign->setContentsMargins(38, 0, 0, 0);
    vLayout->addWidget(otherRoomSign, 0, Qt::AlignmentFlag::AlignLeft);

    otherRoomInput = new QLineEdit(this);
    otherRoomInput->setPlaceholderText("anna@meetup.dev");
    otherRoomInput->setFixedSize(175, 35);
    vLayout->addWidget(otherRoomInput, 0, Qt::AlignmentFlag::AlignCenter);

    askCodeHint = new QLabel("Спросите код у организатора", this);
    vLayout->addWidget(askCodeHint, 0, Qt::AlignmentFlag::AlignCenter);

    enterRoomButton = new QPushButton("Подключиться", this);
    enterRoomButton->setFixedSize(175, 35);
    vLayout->addWidget(enterRoomButton, 0, Qt::AlignmentFlag::AlignCenter);

    orSign = new QLabel("ИЛИ", this);
    vLayout->addWidget(orSign, 0, Qt::AlignmentFlag::AlignCenter);

    createRoomButton = new QPushButton("Создать конференцию", this);
    createRoomButton->setFixedSize(175, 35);
    vLayout->addWidget(createRoomButton, 0, Qt::AlignmentFlag::AlignCenter);

    createRoomHint = new QLabel("Код новой конференции сгенерирует сервер", this);
    vLayout->addWidget(createRoomHint, 0, Qt::AlignmentFlag::AlignCenter);
    vLayout->addStretch();
}
MUMeetingForm::~MUMeetingForm()
{
    delete header;
    delete otherRoomSign;
    delete otherRoomInput;
    delete askCodeHint;
    delete enterRoomButton;
    delete orSign;
    delete createRoomButton;
    delete createRoomHint;
    delete vLayout;
}
