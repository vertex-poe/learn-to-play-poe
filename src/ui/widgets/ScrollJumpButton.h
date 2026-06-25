#pragma once
#include <QPushButton>

class ScrollJumpButton : public QPushButton
{
    Q_OBJECT
public:
    explicit ScrollJumpButton(QWidget *parent = nullptr);
    void setSkipMode(bool skip);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool m_skipMode{false};
};
