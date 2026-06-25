#include "core/Cli.h"
#include "core/AppConfig.h"
#include "db/Database.h"
#include "util/DialogHash.h"
#include "workers/LogIngestWorker.h"
#include "workers/TaskManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static QByteArray readInput(int argc, char *argv[], int fileArgIndex)
{
    if (argc > fileArgIndex) {
        QFile f(QString::fromLocal8Bit(argv[fileArgIndex]));
        if (!f.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << "error: cannot open " << argv[fileArgIndex] << "\n";
            return {};
        }
        return f.readAll();
    }
    QFile in;
    in.open(stdin, QIODevice::ReadOnly);
    return in.readAll();
}

// Builds a single-element JSON array from direct "npc_name" "message" args.
static QJsonArray argsToArray(const QString &npcName, const QString &message)
{
    return QJsonArray{ QJsonObject{ {"npc_name", npcName}, {"message", message} } };
}

static QJsonArray parseInputArray(const QByteArray &raw)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (doc.isNull()) {
        QTextStream(stderr) << "error: " << err.errorString() << "\n";
        return {};
    }
    if (!doc.isArray()) {
        QTextStream(stderr) << "error: expected a JSON array\n";
        return {};
    }
    return doc.array();
}

static QJsonArray hashArray(const QJsonArray &input)
{
    QJsonArray out;
    for (const QJsonValue &val : input) {
        const QJsonObject obj     = val.toObject();
        const QString     npcName = obj["npc_name"].toString();
        const QString     message  = obj["message"].toString();
        out.append(QJsonObject{
            { "npc_name",      npcName },
            { "npc_name_hash", dialogHash(npcName) },
            { "message_hash",  dialogHash(message)  },
        });
    }
    return out;
}

static QString dbPath()
{
    QString p = AppConfig::configPath();
    p.chop(5);
    return p + ".db";
}

// ---------------------------------------------------------------------------
// dialog hash
//
//   l2p-poe1 dialog hash [file.json]
//   l2p-poe1 dialog hash "NPC Name" "message text"
// ---------------------------------------------------------------------------

static int runDialogHash(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QJsonArray input;
    if (argc >= 5) {
        // 1:1 direct args: argv[3] = npc_name, argv[4] = message
        input = argsToArray(
            QString::fromLocal8Bit(argv[3]),
            QString::fromLocal8Bit(argv[4]));
    } else {
        const QByteArray raw = readInput(argc, argv, 3);
        if (raw.isEmpty()) return 1;
        input = parseInputArray(raw);
        if (input.isEmpty()) return 1;
    }

    QTextStream(stdout) << QJsonDocument(hashArray(input)).toJson(QJsonDocument::Indented);
    return 0;
}

// ---------------------------------------------------------------------------
// dialog ingest
//
//   l2p-poe1 dialog ingest [file.json]
//   l2p-poe1 dialog ingest "NPC Name" "message text"
// ---------------------------------------------------------------------------

static int runDialogIngest(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QJsonArray input;
    if (argc >= 5) {
        input = argsToArray(
            QString::fromLocal8Bit(argv[3]),
            QString::fromLocal8Bit(argv[4]));
    } else {
        const QByteArray raw = readInput(argc, argv, 3);
        if (raw.isEmpty()) return 1;
        input = parseInputArray(raw);
        if (input.isEmpty()) return 1;
    }

    Database db(dbPath());
    if (!db.isOpen()) {
        QTextStream(stderr) << "error: " << db.lastError() << "\n";
        return 1;
    }

    const QJsonArray hashed = hashArray(input);
    QList<Database::NpcDialogEntry> entries;
    entries.reserve(hashed.size());
    for (const QJsonValue &val : hashed) {
        const QJsonObject obj = val.toObject();
        entries.append({
            obj["message_hash"].toString(),
            obj["npc_name"].toString(),
            obj["npc_name_hash"].toString(),
            {},
        });
    }

    const int inserted = db.upsertNpcDialogEntries(entries);
    QTextStream(stdout)
        << inserted << " inserted, "
        << (entries.size() - inserted) << " already present\n";
    return 0;
}

// ---------------------------------------------------------------------------
// ingest
// ---------------------------------------------------------------------------

static int runIngest(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");

    const AppConfig config = AppConfig::load();

    Database db(dbPath());
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

        auto *worker = new LogIngestWorker(
            db.path(), inst.id, logPath, inst.lastByteOffset, config.channelNames);
        taskManager->submit(
            QStringLiteral("Ingest %1").arg(logPath), TaskKind::DbWrite, worker);
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

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

static void printUsage()
{
    QTextStream err(stderr);
    err << "usage:\n"
        << "  l2p-poe1 ingest\n"
        << "  l2p-poe1 dialog hash [file.json]\n"
        << "  l2p-poe1 dialog hash <npc_name> <message>\n"
        << "  l2p-poe1 dialog ingest [file.json]\n"
        << "  l2p-poe1 dialog ingest <npc_name> <message>\n";
}

int cliDispatch(int argc, char *argv[])
{
    if (argc < 2)
        return -1;

    const QString verb = QString::fromLocal8Bit(argv[1]);
    const QString noun = argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString();

    if (verb == "ingest")
        return runIngest(argc, argv);

    if (verb == "dialog") {
        if (noun == "hash")   return runDialogHash(argc, argv);
        if (noun == "ingest") return runDialogIngest(argc, argv);
        QTextStream(stderr) << "error: unknown dialog subcommand '" << noun << "'\n";
        printUsage();
        return 1;
    }

    return -1;
}
