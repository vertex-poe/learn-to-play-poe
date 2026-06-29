#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QElapsedTimer g_timer;
static qint64 g_firstPaintAbsMs = 0;

class RefWindow : public QMainWindow {
public:
    RefWindow() {
        m_btn = new QPushButton("No Loading", this);
        setCentralWidget(m_btn);
        
        connect(m_btn, &QPushButton::clicked, this, []() {
            qint64 absMs = g_timer.elapsed();
            qint64 deltaFromPaint = absMs - g_firstPaintAbsMs;
            printf("REF_PERF:first_interaction:%lld:%lld:%lld\n", 
                   absMs, deltaFromPaint, deltaFromPaint);
            fflush(stdout);
            QApplication::quit();
        });
    }

protected:
    void paintEvent(QPaintEvent *e) override {
        QMainWindow::paintEvent(e);
        if (g_firstPaintAbsMs == 0) {
            g_firstPaintAbsMs = g_timer.elapsed();
            printf("REF_PERF:first_paint:%lld:%lld:0\n", 
                   g_firstPaintAbsMs, g_firstPaintAbsMs);
            fflush(stdout);
            
            // Auto-click the button natively using PostMessage
            QTimer::singleShot(10, this, [this]() {
#ifdef Q_OS_WIN
                HWND hwnd = (HWND)m_btn->winId();
                // Send click to the center of the button
                LPARAM lp = MAKELPARAM(m_btn->width() / 2, m_btn->height() / 2);
                PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
                PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
#else
                m_btn->click();
#endif
            });
        }
    }
private:
    QPushButton *m_btn;
};

int main(int argc, char *argv[]) {
    g_timer.start();
    QApplication app(argc, argv);
    
    RefWindow w;
    w.resize(800, 600);
    w.show();
    
    return app.exec();
}
