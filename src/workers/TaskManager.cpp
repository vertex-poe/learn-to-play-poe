#include "workers/TaskManager.h"

#include <QThread>

static constexpr int kMaxDbWriteSlots = 1;
static constexpr int kMaxPooledSlots  = 3;

TaskManager::TaskManager(QObject *parent) : QObject(parent) {}

TaskManager::~TaskManager()
{
    for (auto &record : m_tasks) {
        if (!record.thread) continue;  // already cleaned up via normal finish path
        if (record.worker) record.worker->cancel();
        record.thread->quit();
        record.thread->wait();
        delete record.worker;
        // record.thread is a child of TaskManager; destroyed by parent-child after wait()
    }
}

int TaskManager::submit(const QString &name, TaskKind kind, BackgroundWorker *worker)
{
    worker->setParent(nullptr);

    TaskRecord record;
    record.id     = m_nextId++;
    record.name   = name;
    record.kind   = kind;
    record.status = TaskStatus::Pending;
    record.worker = worker;

    m_tasks.append(record);
    emit taskAdded(record.id);
    tryDequeue();
    return record.id;
}

void TaskManager::cancel(int id)
{
    TaskRecord *record = findTask(id);
    if (!record) return;

    if (record->status == TaskStatus::Pending) {
        delete record->worker;
        record->worker = nullptr;
        record->status = TaskStatus::Cancelled;
        emit taskUpdated(id);
        return;
    }

    if (record->status == TaskStatus::Running || record->status == TaskStatus::Monitoring) {
        record->cancelling = true;
        record->worker->cancel();
    }
}

void TaskManager::cancelAll()
{
    for (const auto &record : std::as_const(m_tasks))
        cancel(record.id);
}

void TaskManager::tryDequeue()
{
    for (auto &record : m_tasks) {
        if (record.status != TaskStatus::Pending) continue;
        if (runningCount(record.kind) >= maxConcurrent(record.kind)) continue;
        startTask(record);
    }
}

void TaskManager::startTask(TaskRecord &record)
{
    auto *thread = new QThread(this);
    record.worker->moveToThread(thread);
    record.thread = thread;
    record.status = TaskStatus::Running;

    const int id = record.id;

    connect(thread, &QThread::started, record.worker, [w = record.worker] { w->start(); });

    connect(record.worker, &BackgroundWorker::progress, this,
        [this, id](int pct, const QString &msg) { onWorkerProgress(id, pct, msg); },
        Qt::QueuedConnection);
    connect(record.worker, &BackgroundWorker::finished, this,
        [this, id] { onWorkerFinished(id); },
        Qt::QueuedConnection);
    connect(record.worker, &BackgroundWorker::failed, this,
        [this, id](const QString &err) { onWorkerFailed(id, err); },
        Qt::QueuedConnection);

    thread->start();
    emit taskUpdated(id);
}

void TaskManager::onWorkerProgress(int id, int percent, const QString &message)
{
    TaskRecord *record = findTask(id);
    if (!record) return;
    record->percent = percent;
    record->message = message;
    if (percent >= 100 && record->status == TaskStatus::Running)
        record->status = TaskStatus::Monitoring;
    else if (percent < 100 && record->status == TaskStatus::Monitoring)
        record->status = TaskStatus::Running;
    emit taskUpdated(id);
}

void TaskManager::onWorkerFinished(int id)
{
    TaskRecord *record = findTask(id);
    if (!record) return;

    record->status  = record->cancelling ? TaskStatus::Cancelled : TaskStatus::Finished;
    record->percent = 100;
    cleanupTask(*record);
    emit taskUpdated(id);
    tryDequeue();
}

void TaskManager::onWorkerFailed(int id, const QString &error)
{
    TaskRecord *record = findTask(id);
    if (!record) return;

    record->status  = TaskStatus::Failed;
    record->message = error;
    cleanupTask(*record);
    emit taskUpdated(id);
    tryDequeue();
}

void TaskManager::cleanupTask(TaskRecord &record)
{
    auto *thread = record.thread;
    auto *worker = record.worker;
    record.thread = nullptr;
    record.worker = nullptr;

    thread->quit();
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
}

int TaskManager::runningCount(TaskKind kind) const
{
    int count = 0;
    for (const auto &r : m_tasks)
        if (r.kind == kind && (r.status == TaskStatus::Running || r.status == TaskStatus::Monitoring))
            ++count;
    return count;
}

int TaskManager::maxConcurrent(TaskKind kind) const
{
    return kind == TaskKind::DbWrite ? kMaxDbWriteSlots : kMaxPooledSlots;
}

TaskRecord *TaskManager::findTask(int id)
{
    for (auto &r : m_tasks)
        if (r.id == id) return &r;
    return nullptr;
}
