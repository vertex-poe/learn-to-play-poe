#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

// Docked notice shown when poe-info-service reports zero configured PoE
// install directories — mirrors TaskPanel's always-in-the-layout,
// visibility-toggled placement in MainWindow. Prompts the user to add one,
// with a button that jumps straight to Settings > Game.
class InstallDirNotice : public QFrame
{
    Q_OBJECT
public:
    explicit InstallDirNotice(QWidget *parent = nullptr);

signals:
    void addClicked();

private:
    QLabel       *m_label{};
    QPushButton  *m_addBtn{};
};
