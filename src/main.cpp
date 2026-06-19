#include "AppConfig.h"
#include "Database.h"
#include "LogIngestWorker.h"
#include "MainWindow.h"
#include "TaskManager.h"
#include "Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>

static int runIngest(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");

    const AppConfig config = AppConfig::load();

    QString dbPath = AppConfig::configPath();
    dbPath.chop(5);
    dbPath += ".db";

    Database db(dbPath);
    if (!db.isOpen()) {
        qCritical() << "DB error:" << db.lastError();
        return 1;
    }

    auto *taskManager = new TaskManager(&app);

    int submitted = 0;
    for (const QString &installDir : config.installDirs) {
        const QString logPath = installDir + "/logs/Client.txt";
        if (!QFileInfo::exists(logPath)) continue;

        const Database::InstallState inst = db.upsertInstall(installDir);
        if (inst.id < 0) continue;

        const QFileInfo fi(logPath);
        const bool upToDate = inst.fileModifiedAt > 0
            && inst.fileModifiedAt == fi.lastModified().toSecsSinceEpoch()
            && inst.fileSize       == fi.size();
        if (upToDate) {
            qInfo().noquote() << "up to date:" << logPath;
            continue;
        }

        auto *worker = new LogIngestWorker(db.path(), inst.id, logPath, inst.lastByteOffset, config.channelNames);
        taskManager->submit(QStringLiteral("Ingest %1").arg(logPath), TaskKind::DbWrite, worker);
        qInfo().noquote() << "ingesting:" << logPath;
        ++submitted;
    }

    if (submitted == 0) {
        qInfo() << "nothing to ingest";
        return 0;
    }

    QObject::connect(taskManager, &TaskManager::taskUpdated, [taskManager](int) {
        for (const auto &r : taskManager->tasks()) {
            if (r.status == TaskStatus::Pending || r.status == TaskStatus::Running)
                return;
        }
        QCoreApplication::quit();
    });

    QObject::connect(taskManager, &TaskManager::taskUpdated, [taskManager](int id) {
        for (const auto &r : taskManager->tasks()) {
            if (r.id != id) continue;
            if (r.status == TaskStatus::Running && r.percent > 0)
                qInfo().noquote() << QStringLiteral("%1%  %2").arg(r.percent, 3).arg(r.message);
            break;
        }
    });

    return app.exec();
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--ingest")
            return runIngest(argc, argv);
    }

    QApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);

    Theme::apply(app);

    MainWindow window;
    if (!window.startMinimized())
        window.show();

    return app.exec();
}
