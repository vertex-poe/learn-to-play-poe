#pragma once

#include "workers/BackgroundWorker.h"

// A BackgroundWorker that does no work of its own — it exists purely so the
// existing TaskManager/TaskPanel progress-bar UI can be driven by progress
// computed elsewhere (e.g. poe-info-service's pushed Client.txt ingest
// "status" events, see MainWindow::applyStatusPayload) instead of a real
// worker thread doing the work itself.
//
// start() is a no-op — TaskManager still moves this to its own QThread and
// calls it, but the thread just sits idle until reportFinished() is called.
// reportProgress/reportFinished are safe to call from the main thread (where
// MainWindow lives) even though the object's thread affinity is the worker
// thread: TaskManager connects BackgroundWorker's signals with
// Qt::QueuedConnection, so emitting them isn't sensitive to which thread the
// call happens on, only which thread the connected slot runs on.
class ProgressTrackerWorker : public BackgroundWorker
{
    Q_OBJECT
public:
    using BackgroundWorker::BackgroundWorker;

    void start() override {}

    void reportProgress(int percent, const QString &message) { emit progress(percent, message); }
    void reportFinished() { emit finished(); }
};
