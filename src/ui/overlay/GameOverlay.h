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

signals:
    void showMainWindowRequested();

public:
    // Reposition and resize the overlay to cover the given game window rect (physical px on Windows).
    void updateGameRect(const QRect &physicalRect);

    // Show overlay when the game window is present, hide otherwise.
    void setGameVisible(bool found);

    void setLayoutGrid(int columns, int rows);
    void setHideoutVisible(bool visible);
    void setGuildVisible(bool visible);
    void setMenagerieVisible(bool visible);
    void setMonasteryVisible(bool visible);
    void setHeistVisible(bool visible);
    void setSanctumVisible(bool visible);
    void setLadderVisible(bool visible);
    void setDelveVisible(bool visible);
    void setKingsmarchVisible(bool visible);
    void setTimePlayedVisible(bool visible);
    void setCharacterAgeVisible(bool visible);
    void setPassivesVisible(bool visible);
    void setDeathsVisible(bool visible);
    void setMonstersRemainingVisible(bool visible);
    void setAtlasPassivesVisible(bool visible);
    void setKillsVisible(bool visible);
    void setResetXPVisible(bool visible);
    void setReloadItemFilterVisible(bool visible);
    void setL2PVisible(bool visible);
    void setGameHwnd(quint64 hwnd);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void repositionPanels();
    void updateMask();

    QWidget *m_panelContainer{};
    QWidget *m_infoPanel{};
    QWidget *m_hideoutIcon{};
    QWidget *m_guildIcon{};
    QWidget *m_menagerieIcon{};
    QWidget *m_monasteryIcon{};
    QWidget *m_heistIcon{};
    QWidget *m_sanctumIcon{};
    QWidget *m_ladderIcon{};
    QWidget *m_delveIcon{};
    QWidget *m_kingsmarchIcon{};
    QWidget *m_timeplayedIcon{};
    QWidget *m_characterageIcon{};
    QWidget *m_passivesIcon{};
    QWidget *m_deathsIcon{};
    QWidget *m_monstersremainingIcon{};
    QWidget *m_atlaspassivesIcon{};
    QWidget *m_killsIcon{};
    QWidget *m_resetxpIcon{};
    QWidget *m_reloaditemfilterIcon{};

    int m_gridColumns{1};
    int m_gridRows{0};
    void rebuildGridLayout();

#ifdef _WIN32
    OverlayKeepalive *m_keepalive{};
#endif
};
