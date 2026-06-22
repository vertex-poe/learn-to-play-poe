#ifdef _WIN32

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "OverlayKeepalive.h"

static constexpr int kIntervalMs = 500;

OverlayKeepalive::OverlayKeepalive(void *hwnd)
    : m_hwnd(hwnd)
{
    m_thread = std::thread(&OverlayKeepalive::run, this);
}

OverlayKeepalive::~OverlayKeepalive()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop.store(true, std::memory_order_relaxed);
    }
    m_cv.notify_all();
    m_thread.join();
}

void OverlayKeepalive::run()
{
    const auto hwnd = static_cast<HWND>(m_hwnd);
    std::unique_lock<std::mutex> lock(m_mutex);

    while (!m_cv.wait_for(lock, std::chrono::milliseconds(kIntervalMs),
                           [this] { return m_stop.load(std::memory_order_relaxed); })) {
        lock.unlock();

        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

        const LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
        if (!(ex & WS_EX_LAYERED) || !(ex & WS_EX_TRANSPARENT))
            SetWindowLong(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);

        lock.lock();
    }
}

#endif // Q_OS_WIN
