#pragma once

#include <QObject>

// Event filter installed on a page widget. Detects paint events and forwards
// them to PerfProbe based on role (default page or swap target page).
class PaintProbeFilter : public QObject
{
    Q_OBJECT
public:
    enum Role { Default, Swap };

    explicit PaintProbeFilter(Role role, QObject *parent = nullptr)
        : QObject(parent), m_role(role) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    Role m_role;
};
