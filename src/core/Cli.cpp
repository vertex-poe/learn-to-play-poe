#include "core/Cli.h"
#include "util/DialogHash.h"

#include <QCoreApplication>
#include <QFile>
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

// ---------------------------------------------------------------------------
// dialog hash
//
//   l2p-poe dialog hash [file.json]
//   l2p-poe dialog hash "NPC Name" "message text"
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
// Dispatcher
// ---------------------------------------------------------------------------

// dialog ingest writes to the database, which poe-info-service owns
// exclusively (ADR-006) — that subcommand lives on poe-info-service's own
// CLI now (`poe-info-service dialog ingest`), reading the JSON this
// `dialog hash` prints. This binary never touches the database.
static void printUsage()
{
    QTextStream err(stderr);
    err << "usage:\n"
        << "  l2p-poe dialog hash [file.json]\n"
        << "  l2p-poe dialog hash <npc_name> <message>\n"
        << "\n"
        << "To write hashed entries into the database, pipe into\n"
        << "poe-info-service's own CLI instead:\n"
        << "  l2p-poe dialog hash file.json | poe-info-service dialog ingest\n";
}

int cliDispatch(int argc, char *argv[])
{
    if (argc < 2)
        return -1;

    const QString verb = QString::fromLocal8Bit(argv[1]);
    const QString noun = argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString();

    if (verb == "dialog") {
        if (noun == "hash") return runDialogHash(argc, argv);
        QTextStream(stderr) << "error: unknown dialog subcommand '" << noun << "'\n";
        printUsage();
        return 1;
    }

    return -1;
}
