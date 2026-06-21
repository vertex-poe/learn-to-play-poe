#pragma once
#include <QPushButton>

class ScrollJumpButton : public QPushButton
{
    Q_OBJECT
public:
    explicit ScrollJumpButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
};
