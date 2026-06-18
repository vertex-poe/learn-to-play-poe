#include "WindowTracker.h"

#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

namespace {

struct EnumData {
    const QStringList &targets;
    WindowState        result;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return TRUE;

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return TRUE;

    wchar_t buf[MAX_PATH] = {};
    DWORD   size          = MAX_PATH;
    if (QueryFullProcessImageNameW(proc, 0, buf, &size)) {
        const QString fullPath = QString::fromWCharArray(buf, static_cast<int>(size));
        const QString baseName = QFileInfo(fullPath).fileName();
        auto *data = reinterpret_cast<EnumData *>(lParam);
        for (const QString &target : data->targets) {
            if (baseName.compare(target, Qt::CaseInsensitive) == 0) {
                data->result.found          = true;
                data->result.executableName = baseName;
                data->result.installDir     = QFileInfo(fullPath).absolutePath();
                if (!IsIconic(hwnd)) {
                    RECT r = {};
                    GetWindowRect(hwnd, &r);
                    data->result.rect = QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
                }
                CloseHandle(proc);
                return FALSE; // stop enumeration
            }
        }
    }

    CloseHandle(proc);
    return TRUE;
}

} // namespace

class WindowTrackerWindows : public WindowTracker {
public:
    WindowState poll(const QStringList &executableNames) override
    {
        EnumData data{executableNames, {}};
        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
        return data.result;
    }
};

WindowTracker *WindowTracker::create()
{
    return new WindowTrackerWindows();
}
