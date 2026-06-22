#pragma once

#include <QWidget>

#ifdef _WIN32
class OverlayKeepalive;
#endif

class GameOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit GameOverlay(QWidget *parent = nullptr);
    ~GameOverlay() override;

    // Reposition and resize the overlay to cover the given game window rect (physical px on Windows).
    void updateGameRect(const QRect &physicalRect);

    // Show overlay when the game window is present, hide otherwise.
    void setGameVisible(bool found);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void repositionPanels();
    void updateMask();

    QWidget *m_infoPanel{};

#ifdef _WIN32
    OverlayKeepalive *m_keepalive{};
#endif
};
