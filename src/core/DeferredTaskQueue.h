#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QTimer>
#include <functional>

/// Singleton queue that runs deferred work on the UI thread, highest
/// priority first, one task per timer tick.
///
/// Tasks are keyed by a string id. Re-enqueuing an existing id merges
/// into the existing task rather than adding a duplicate.
///
/// Requestor tracking
/// ------------------
/// A task may carry a set of requestors (QObject*), each with the priority
/// it requested. The task's effective priority is the max over all requestor
/// priorities. Passing requestor=nullptr means "untracked": the task's
/// priority is set directly and it is never auto-cancelled.
class DeferredTaskQueue : public QObject
{
    Q_OBJECT

public:
    /// Increasing order. Gap before Immediate leaves room for future levels
    /// without renumbering; integer comparison drives scheduling.
    enum Priority {
        Eventual  = 0,
        Low       = 1,
        Medium    = 2,
        High      = 3,
        Immediate = 10
    };

    static DeferredTaskQueue& instance();

    /// Enqueue or merge into the task identified by id.
    ///
    /// priority is the priority being requested by this call:
    ///   - requestor == nullptr (untracked): sets task priority directly.
    ///     If the task was previously tracked its requestors are cleared —
    ///     an explicit user-action enqueue supersedes background preloads.
    ///   - requestor != nullptr (tracked): records this requestor at priority;
    ///     effective priority becomes the max over all current requestors.
    void enqueue(const QString& id, int priority,
                 std::function<void()> task, QObject* requestor = nullptr);

    /// Force the effective priority of id, bypassing the requestor map.
    /// Intended for interaction-driven demotions (hide, navigate-back).
    /// Reconciled to the requestor max on the next requestor mutation.
    void setPriority(const QString& id, int priority);

    /// Remove the task id unconditionally, regardless of requestors.
    void cancel(const QString& id);

    /// Remove requestor from every tracked task. Tasks left with no
    /// requestors are dropped and their priority recalculated. Untracked
    /// tasks and a nullptr requestor are ignored.
    void cancelByRequestor(QObject* requestor);

private slots:
    void processNextTask();

private:
    explicit DeferredTaskQueue(QObject *parent = nullptr);
    ~DeferredTaskQueue() override = default;

    struct Task {
        QString id;
        int priority = Eventual;
        std::function<void()> work;
        QMap<QObject*, int> requestors; // requestor → requested priority
        bool tracked = false;           // ever had a requestor; gates orphan check at dequeue
    };

    Task* findTask(const QString& id);
    void  recalcPriority(Task& t);
    void  updateWaitCursor();

    QList<Task> m_tasks;
    QTimer m_timer;
    bool m_waitCursorActive = false;
};
