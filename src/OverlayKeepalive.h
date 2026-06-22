#pragma once
#ifdef _WIN32

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Background thread that re-asserts HWND_TOPMOST and WS_EX_LAYERED|WS_EX_TRANSPARENT
// on the overlay window every kIntervalMs milliseconds. This keeps the overlay
// pinned and click-through through a brief UI-thread wedge without tearing the
// window down or stealing game focus (SWP_NOACTIVATE is always passed).
//
// HWND creation/destruction must still happen on the Qt GUI thread. Re-asserting
// window state from another thread is safe on Win32.
class OverlayKeepalive
{
public:
    explicit OverlayKeepalive(void *hwnd); // void* to avoid pulling <windows.h> into header
    ~OverlayKeepalive();

private:
    void run();

    void *                  m_hwnd;
    std::atomic<bool>       m_stop{false};
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::thread             m_thread;
};

#endif // Q_OS_WIN
