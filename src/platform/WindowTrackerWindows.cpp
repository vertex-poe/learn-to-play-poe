#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include "platform/WindowTracker.h"

#include <QFileInfo>
#include <QSet>

namespace {

struct EnumData {
    const QStringList &targets;
    QSet<quint32>      seenPids;
    QList<WindowState> results;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return TRUE;

    auto *data = reinterpret_cast<EnumData *>(lParam);
    if (data->seenPids.contains(static_cast<quint32>(pid)))
        return TRUE; // already recorded this PID via an earlier window

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return TRUE;

    wchar_t buf[MAX_PATH] = {};
    DWORD   size          = MAX_PATH;
    if (QueryFullProcessImageNameW(proc, 0, buf, &size)) {
        const QString fullPath = QString::fromWCharArray(buf, static_cast<int>(size));
        const QString baseName = QFileInfo(fullPath).fileName();
        for (const QString &target : data->targets) {
            if (baseName.compare(target, Qt::CaseInsensitive) == 0) {
                WindowState state;
                state.executableName = baseName;
                state.installDir     = QFileInfo(fullPath).absolutePath();
                state.pid            = static_cast<quint32>(pid);
                if (!IsIconic(hwnd)) {
                    RECT r = {};
                    GetWindowRect(hwnd, &r);
                    state.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
                }
                FILETIME creation, exit, kernel, user;
                if (GetProcessTimes(proc, &creation, &exit, &kernel, &user)) {
                    SYSTEMTIME utc, local;
                    FileTimeToSystemTime(&creation, &utc);
                    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
                    state.startedAt = QStringLiteral("%1:%2")
                        .arg(local.wHour,   2, 10, QChar('0'))
                        .arg(local.wMinute, 2, 10, QChar('0'));
                }
                data->seenPids.insert(state.pid);
                data->results.append(state);
                break;
            }
        }
    }

    CloseHandle(proc);
    return TRUE; // continue — collect all matching PIDs
}

} // namespace

class WindowTrackerWindows : public WindowTracker {
public:
    QList<WindowState> poll(const QStringList &executableNames) override
    {
        EnumData data{executableNames, {}, {}};
        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
        return data.results;
    }
};

WindowTracker *WindowTracker::create()
{
    return new WindowTrackerWindows();
}
