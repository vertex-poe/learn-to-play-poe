#pragma once

#include <QStringList>
#include <QWidget>

class NavBar : public QWidget
{
    Q_OBJECT

public:
    explicit NavBar(const QStringList &labels, QWidget *parent = nullptr);

    int  currentIndex() const { return m_current; }
    bool searchActive()   const { return m_searchActive; }
    void setCurrentIndex(int index);
    void setGearActive(bool active);
    void setSearchActive(bool active);

signals:
    void currentChanged(int index);
    void settingsClicked();
    void searchClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;

private:
    static constexpr int k_listWidth = 48;
    static constexpr int k_gearWidth = 48;
    QStringList m_labels;
    int  m_current{0};
    bool m_searchActive{false};
    bool m_gearActive{false};
};
