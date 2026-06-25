#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

struct WindowState {
    QRect    rect;
    QString  installDir;
    QString  executableName;
    QString  startedAt;   // "HH:mm" local time the process was created; empty if unavailable
    quint32  pid{0};
};

class WindowTracker {
public:
    virtual ~WindowTracker() = default;

    // Returns all currently-running instances of any named target process.
    // executableNames is a list of candidate bare filenames (e.g. "PathOfExile.exe").
    virtual QList<WindowState> poll(const QStringList &executableNames) = 0;

    static WindowTracker *create();
};
