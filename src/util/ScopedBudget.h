#pragma once
#include <QElapsedTimer>
#include <QtDebug>

// RAII frame-budget guard. Warns at destruction if elapsed > budgetMs.
// Drop at the top of any slot that runs on the UI thread to catch regressions early.
//
//   void MyPage::rebuild() {
//       ScopedBudget budget("MyPage::rebuild");
//       ...
//   }
class ScopedBudget {
public:
    explicit ScopedBudget(const char *name, qint64 budgetMs = 50)
        : m_name(name), m_budgetMs(budgetMs)
    { m_timer.start(); }

    ~ScopedBudget()
    {
        const qint64 ms = m_timer.elapsed();
        if (ms > m_budgetMs)
            qWarning("[budget] EXCEEDED %s — %lld ms (budget %lld ms)",
                     m_name, static_cast<long long>(ms), static_cast<long long>(m_budgetMs));
    }

    Q_DISABLE_COPY_MOVE(ScopedBudget)

private:
    const char   *m_name;
    qint64        m_budgetMs;
    QElapsedTimer m_timer;
};
