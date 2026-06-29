#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QListWidget>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <cstdio>
#include <sqlite3.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QElapsedTimer g_timer;
static qint64 g_firstPaintAbsMs = 0;
static qint64 g_firstInteractionAbsMs = 0;
static qint64 g_firstLoadAbsMs = 0;
static qint64 g_finalPaintAbsMs = 0;

static void sendNativeClick(QWidget *w) {
#ifdef Q_OS_WIN
    HWND hwnd = (HWND)w->winId();
    LPARAM lp = MAKELPARAM(w->width() / 2, w->height() / 2);
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    PostMessageW(hwnd, WM_LBUTTONUP, 0, lp);
#endif
}

class RefDataWindow : public QMainWindow {
    Q_OBJECT
public:
    RefDataWindow(const QString &dbPath) : m_dbPath(dbPath) {
        QWidget *central = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(central);
        
        m_btnStart = new QPushButton("No Loading", this);
        m_listWidget = new QListWidget(this);
        m_btnFinal = new QPushButton("Final Interaction", this);
        
        layout->addWidget(m_btnStart);
        layout->addWidget(m_listWidget);
        layout->addWidget(m_btnFinal);
        setCentralWidget(central);
        
        connect(m_btnStart, &QPushButton::clicked, this, &RefDataWindow::onStartClicked);
        connect(m_btnFinal, &QPushButton::clicked, this, &RefDataWindow::onFinalClicked);
        connect(&m_watcher, &QFutureWatcher<QStringList>::finished, this, &RefDataWindow::onDataLoaded);
    }

protected:
    void paintEvent(QPaintEvent *e) override {
        QMainWindow::paintEvent(e);
        if (g_firstPaintAbsMs == 0) {
            g_firstPaintAbsMs = g_timer.elapsed();
            printf("REF_PERF:first_paint:%lld:%lld:0\n", 
                   g_firstPaintAbsMs, g_firstPaintAbsMs);
            fflush(stdout);
            
            QTimer::singleShot(10, this, [this]() { sendNativeClick(m_btnStart); });
        } else if (g_firstLoadAbsMs > 0 && g_finalPaintAbsMs == 0) {
            g_finalPaintAbsMs = g_timer.elapsed();
            qint64 delta = g_finalPaintAbsMs - g_firstLoadAbsMs;
            qint64 deltaPaint = g_finalPaintAbsMs - g_firstPaintAbsMs;
            printf("REF_PERF:final_paint:%lld:%lld:%lld\n", 
                   g_finalPaintAbsMs, delta, deltaPaint);
            fflush(stdout);
            
            QTimer::singleShot(10, this, [this]() { sendNativeClick(m_btnFinal); });
        }
    }

private slots:
    void onStartClicked() {
        if (g_firstInteractionAbsMs != 0) return;
        g_firstInteractionAbsMs = g_timer.elapsed();
        qint64 delta = g_firstInteractionAbsMs - g_firstPaintAbsMs;
        printf("REF_PERF:first_interaction:%lld:%lld:%lld\n", 
               g_firstInteractionAbsMs, delta, delta);
        fflush(stdout);
        
        QFuture<QStringList> future = QtConcurrent::run([this]() -> QStringList {
            QStringList results;
            sqlite3 *db;
            if (sqlite3_open(m_dbPath.toUtf8().constData(), &db) == SQLITE_OK) {
                sqlite3_stmt *stmt;
                if (sqlite3_prepare_v2(db, "SELECT value FROM data LIMIT 40", -1, &stmt, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        results << QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(db);
            }
            return results;
        });
        m_watcher.setFuture(future);
    }
    
    void onDataLoaded() {
        g_firstLoadAbsMs = g_timer.elapsed();
        qint64 delta = g_firstLoadAbsMs - g_firstInteractionAbsMs;
        qint64 deltaPaint = g_firstLoadAbsMs - g_firstPaintAbsMs;
        printf("REF_PERF:first_load:%lld:%lld:%lld\n", 
               g_firstLoadAbsMs, delta, deltaPaint);
        fflush(stdout);
        
        m_listWidget->addItems(m_watcher.result());
        update(); // Trigger re-paint
    }
    
    void onFinalClicked() {
        qint64 absMs = g_timer.elapsed();
        qint64 delta = absMs - g_finalPaintAbsMs;
        qint64 deltaPaint = absMs - g_firstPaintAbsMs;
        printf("REF_PERF:final_interaction:%lld:%lld:%lld\n", 
               absMs, delta, deltaPaint);
        fflush(stdout);
        QApplication::quit();
    }

private:
    QString m_dbPath;
    QPushButton *m_btnStart;
    QListWidget *m_listWidget;
    QPushButton *m_btnFinal;
    QFutureWatcher<QStringList> m_watcher;
};

int main(int argc, char *argv[]) {
    g_timer.start();
    QApplication app(argc, argv);
    
    QString dbPath = (argc > 1) ? QString::fromUtf8(argv[1]) : QString();
    
    RefDataWindow w(dbPath);
    w.resize(800, 600);
    w.show();
    
    return app.exec();
}
#include "main.moc"
