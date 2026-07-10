#pragma once

#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

class MUMeetingForm: public QWidget
{
    Q_OBJECT
public:
    MUMeetingForm(QWidget* parent);
    ~MUMeetingForm();
private:
    QVBoxLayout* vLayout;
    QLabel* header;
    QLabel* otherRoomSign;
    QLineEdit* otherRoomInput;
    QLabel* askCodeHint;
    QPushButton* enterRoomButton;
    QLabel* orSign;
    QPushButton* createRoomButton;
    QLabel* createRoomHint;
};

class MUHomePage: public QWidget
{
    Q_OBJECT
public:
    MUHomePage(QWidget* parent);
    ~MUHomePage();
private:
    QVBoxLayout* vLayout;
    QPushButton* crMeetingButton;
    MUMeetingForm* meetingForm;
};
