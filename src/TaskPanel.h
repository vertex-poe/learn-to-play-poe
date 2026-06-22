#pragma once

#include "TaskManager.h"

#include <QFrame>
#include <QMap>
#include <QSet>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

class TaskPanel : public QFrame
{
    Q_OBJECT
public:
    explicit TaskPanel(TaskManager *manager, QWidget *parent = nullptr);

public slots:
    void setForcedVisible(bool forced);

private:
    void onTaskAdded(int id);
    void onTaskUpdated(int id);

    void addRow(const TaskRecord &record);
    void updateRow(const TaskRecord &record);
    void removeRow(int id);
    void refreshVisibility();

    struct Row {
        QWidget      *widget    {};
        QLabel       *name      {};
        QProgressBar *bar       {};
        QLabel       *message   {};
        QPushButton  *cancelBtn {};
    };

    TaskManager       *m_manager;
    QVBoxLayout       *m_layout;
    QMap<int, Row>     m_rows;
    QSet<int>          m_pendingShow;   // ids waiting for the 100 ms visibility threshold
    bool               m_forcedVisible{false};
};
