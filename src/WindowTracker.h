#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

struct WindowState {
    bool    found{false};
    QRect   rect;
    QString installDir;
    QString executableName; // which name from the candidates list was matched
};

class WindowTracker {
public:
    virtual ~WindowTracker() = default;

    // Returns current state of the target process window.
    // executableNames is a list of candidate bare filenames (e.g. "PathOfExile.exe").
    virtual WindowState poll(const QStringList &executableNames) = 0;

    static WindowTracker *create();
};
