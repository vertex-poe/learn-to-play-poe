#include <QtTest/QtTest>

#include "workers/ProgressTrackerWorker.h"
#include "workers/TaskManager.h"

// Covers the pattern MainWindow uses to drive the TaskPanel progress bar from
// poe-info-service's pushed ingest "status" events (see
// MainWindow::applyStatusPayload/startProgressTracker): a
// ProgressTrackerWorker does no work of its own — TaskManager still runs it
// on a QThread, but progress/finished are reported externally via
// reportProgress/reportFinished instead of by real work happening.
class TestTaskManager : public QObject
{
    Q_OBJECT
private slots:
    void submitStartsImmediately()
    {
        TaskManager manager;
        auto *worker = new ProgressTrackerWorker();
        const int id = manager.submit("Processing game logs", TaskKind::General, worker);

        const TaskRecord *record = findRecord(manager, id);
        QVERIFY(record);
        QCOMPARE(record->status, TaskStatus::Running);
    }

    void reportProgressUpdatesPercentAndMessage()
    {
        TaskManager manager;
        auto *worker = new ProgressTrackerWorker();
        const int id = manager.submit("Processing game logs", TaskKind::General, worker);

        worker->reportProgress(42, "processing game logs");
        QTRY_COMPARE(findRecord(manager, id)->percent, 42);
        QCOMPARE(findRecord(manager, id)->message, QString("processing game logs"));

        worker->reportProgress(77, "still processing");
        QTRY_COMPARE(findRecord(manager, id)->percent, 77);
        QCOMPARE(findRecord(manager, id)->message, QString("still processing"));
    }

    void reportFinishedMarksTheTaskFinished()
    {
        // TaskManager keeps every task record for the process's lifetime —
        // it's TaskPanel that removes the row once status is terminal (see
        // TaskPanel::onTaskUpdated). This only covers TaskManager's own
        // contract: reportFinished must flip status to Finished at 100%.
        TaskManager manager;
        auto *worker = new ProgressTrackerWorker();
        const int id = manager.submit("Processing game logs", TaskKind::General, worker);
        worker->reportProgress(10, "working");
        QTRY_COMPARE(findRecord(manager, id)->percent, 10);

        worker->reportFinished();
        QTRY_COMPARE(findRecord(manager, id)->status, TaskStatus::Finished);
        QCOMPARE(findRecord(manager, id)->percent, 100);

        // cleanupTask() (see TaskManager.cpp) quits the now-idle worker
        // thread and deleteLater()s it asynchronously; give the event loop a
        // moment to actually finish that before manager's destructor runs at
        // the end of this scope; destroying a QThread that hasn't finished
        // quitting yet is a real (if narrow) crash risk.
        QTest::qWait(100);
    }

private:
    static const TaskRecord *findRecord(const TaskManager &manager, int id)
    {
        for (const auto &r : manager.tasks())
            if (r.id == id) return &r;
        return nullptr;
    }
};

QTEST_MAIN(TestTaskManager)
#include "test_task_manager.moc"
