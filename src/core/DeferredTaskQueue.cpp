#include "core/DeferredTaskQueue.h"

#include <QGuiApplication>
#include <QCursor>

DeferredTaskQueue& DeferredTaskQueue::instance()
{
    static DeferredTaskQueue inst;
    return inst;
}

DeferredTaskQueue::DeferredTaskQueue(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &DeferredTaskQueue::processNextTask);
}

DeferredTaskQueue::Task* DeferredTaskQueue::findTask(const QString& id)
{
    for (auto& t : m_tasks)
        if (t.id == id) return &t;
    return nullptr;
}

void DeferredTaskQueue::recalcPriority(Task& t)
{
    int max = Eventual;
    for (int p : t.requestors.values())
        max = qMax(max, p);
    t.priority = max;
}

void DeferredTaskQueue::enqueue(const QString& id, int priority,
                                std::function<void()> task, QObject* requestor)
{
    if (Task* t = findTask(id)) {
        t->work = std::move(task);
        if (requestor) {
            t->requestors[requestor] = priority;
            t->tracked = true;
            recalcPriority(*t);
        } else {
            t->requestors.clear();
            t->tracked = false;
            t->priority = priority;
        }
    } else {
        Task newTask;
        newTask.id = id;
        newTask.work = std::move(task);
        if (requestor) {
            newTask.requestors[requestor] = priority;
            newTask.tracked = true;
            newTask.priority = priority;
        } else {
            newTask.priority = priority;
        }
        m_tasks.append(std::move(newTask));
    }

    if (!m_timer.isActive())
        m_timer.start();
    updateWaitCursor();
}

void DeferredTaskQueue::setPriority(const QString& id, int priority)
{
    if (Task* t = findTask(id)) {
        t->priority = priority;
        if (!m_timer.isActive())
            m_timer.start();
        updateWaitCursor();
    }
}

void DeferredTaskQueue::cancel(const QString& id)
{
    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
        [&id](const Task& t) { return t.id == id; }), m_tasks.end());

    if (m_tasks.isEmpty())
        m_timer.stop();
    updateWaitCursor();
}

void DeferredTaskQueue::cancelByRequestor(QObject* requestor)
{
    if (!requestor) return;

    auto it = m_tasks.begin();
    while (it != m_tasks.end()) {
        if (!it->requestors.contains(requestor)) {
            ++it;
            continue;
        }
        it->requestors.remove(requestor);
        if (it->requestors.isEmpty()) {
            it = m_tasks.erase(it);
        } else {
            recalcPriority(*it);
            ++it;
        }
    }

    if (m_tasks.isEmpty())
        m_timer.stop();
    updateWaitCursor();
}

void DeferredTaskQueue::processNextTask()
{
    if (m_tasks.isEmpty())
        return;

    auto highestIt = m_tasks.begin();
    for (auto it = m_tasks.begin() + 1; it != m_tasks.end(); ++it) {
        if (it->priority > highestIt->priority)
            highestIt = it;
    }

    Task task = std::move(*highestIt);
    m_tasks.erase(highestIt);

    if (!m_tasks.isEmpty())
        m_timer.start();

    // Discard orphaned tracked tasks whose last requestor was already removed
    // via cancelByRequestor but which re-entered the queue through a delayed enqueue.
    if (task.tracked && task.requestors.isEmpty()) {
        updateWaitCursor();
        return;
    }

    if (task.work)
        task.work();

    updateWaitCursor();
}

void DeferredTaskQueue::updateWaitCursor()
{
    bool needsWaitCursor = false;
    for (const auto& t : m_tasks) {
        if (t.priority >= Immediate) {
            needsWaitCursor = true;
            break;
        }
    }

    if (needsWaitCursor && !m_waitCursorActive) {
        m_waitCursorActive = true;
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    } else if (!needsWaitCursor && m_waitCursorActive) {
        m_waitCursorActive = false;
        QGuiApplication::restoreOverrideCursor();
    }
}
