#include "platform/WindowTracker.h"

#include <QFileInfo>
#include <QList>
#include <QSet>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstring>
#include <filesystem>

namespace {

// Returns the PID stored in _NET_WM_PID for the given window, or 0 on failure.
static unsigned long getWindowPid(Display *dpy, Window win)
{
    Atom           netWmPid = XInternAtom(dpy, "_NET_WM_PID", True);
    if (netWmPid == None)
        return 0;

    Atom           actualType;
    int            actualFormat;
    unsigned long  nItems, bytesAfter;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, win, netWmPid, 0, 1, False, XA_CARDINAL,
                           &actualType, &actualFormat, &nItems,
                           &bytesAfter, &prop) == Success
        && prop && nItems == 1) {
        unsigned long pid = *reinterpret_cast<unsigned long *>(prop);
        XFree(prop);
        return pid;
    }
    if (prop)
        XFree(prop);
    return 0;
}

// Walks the X11 window tree rooted at `root`, collecting all windows whose
// owning process matches any name in `executableNames`.
static void walkTree(Display *dpy, Window root, const QStringList &executableNames,
                     QList<WindowState> &results, QSet<quint32> &seenPids)
{
    Window       rootReturn, parentReturn;
    Window      *children = nullptr;
    unsigned int nChildren = 0;

    if (!XQueryTree(dpy, root, &rootReturn, &parentReturn, &children, &nChildren))
        return;

    for (unsigned int i = 0; i < nChildren; ++i) {
        unsigned long pid = getWindowPid(dpy, children[i]);
        if (pid > 0 && !seenPids.contains(static_cast<quint32>(pid))) {
            const std::string exeLink = "/proc/" + std::to_string(pid) + "/exe";
            char buf[PATH_MAX]        = {};
            ssize_t len = ::readlink(exeLink.c_str(), buf, sizeof(buf) - 1);
            if (len > 0) {
                const QString fullPath = QString::fromLocal8Bit(buf, static_cast<int>(len));
                const QString baseName = QFileInfo(fullPath).fileName();
                for (const QString &name : executableNames) {
                    if (baseName.compare(name, Qt::CaseInsensitive) == 0) {
                        XWindowAttributes attrs = {};
                        if (XGetWindowAttributes(dpy, children[i], &attrs)
                            && attrs.map_state == IsViewable) {
                            int    sx = 0, sy = 0;
                            Window child;
                            XTranslateCoordinates(dpy, children[i],
                                                  XDefaultRootWindow(dpy),
                                                  0, 0, &sx, &sy, &child);
                            WindowState state;
                            state.executableName = baseName;
                            state.rect           = QRect(sx, sy, attrs.width, attrs.height);
                            state.installDir     = QFileInfo(fullPath).absolutePath();
                            state.pid            = static_cast<quint32>(pid);
                            seenPids.insert(state.pid);
                            results.append(state);
                        }
                        break;
                    }
                }
            }
        }

        walkTree(dpy, children[i], executableNames, results, seenPids);
    }

    if (children)
        XFree(children);
}

} // namespace

class WindowTrackerLinux : public WindowTracker {
public:
    QList<WindowState> poll(const QStringList &executableNames) override
    {
        QList<WindowState> results;
        Display *dpy = XOpenDisplay(nullptr);
        if (!dpy)
            return results;

        QSet<quint32> seenPids;
        walkTree(dpy, XDefaultRootWindow(dpy), executableNames, results, seenPids);
        XCloseDisplay(dpy);
        return results;
    }
};

WindowTracker *WindowTracker::create()
{
    return new WindowTrackerLinux();
}
