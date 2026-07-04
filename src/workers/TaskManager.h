#pragma once

#include "workers/BackgroundWorker.h"

#include <QObject>
#include <QList>

class QThread;

enum class TaskKind {
    DbWrite,  // serialized — max 1 concurrent (SQLite single-writer)
    Network,  // pooled    — max 3 concurrent
    General,  // pooled    — max 3 concurrent
};

enum class TaskStatus {
    Pending,
    Running,
    Monitoring,  // running but caught up — task row is hidden
    Finished,
    Failed,
    Cancelled,
};

struct TaskRecord {
    int               id       {};
    QString           name     {};
    TaskKind          kind     {};
    TaskStatus        status   {};
    int               percent  {};
    QString           message  {};
    bool              cancelling{false};
    QThread          *thread   {nullptr};
    BackgroundWorker *worker   {nullptr};
    qint64            startedAtMs{}; // QDateTime::currentMSecsSinceEpoch() when status became Running
};

class TaskManager : public QObject
{
    Q_OBJECT
public:
    explicit TaskManager(QObject *parent = nullptr);
    ~TaskManager() override;

    // Takes ownership of worker. Returns the assigned task ID.
    int  submit(const QString &name, TaskKind kind, BackgroundWorker *worker);
    void cancel(int id);
    void cancelAll();

    const QList<TaskRecord> &tasks() const { return m_tasks; }

signals:
    void taskAdded(int id);
    void taskUpdated(int id);

private:
    void tryDequeue();
    void startTask(TaskRecord &record);
    void onWorkerProgress(int id, int percent, const QString &message);
    void onWorkerFinished(int id);
    void onWorkerFailed(int id, const QString &error);
    void cleanupTask(TaskRecord &record);

    int  runningCount(TaskKind kind) const;
    int  maxConcurrent(TaskKind kind) const;
    TaskRecord *findTask(int id);

    QList<TaskRecord> m_tasks;
    int               m_nextId{1};
};
