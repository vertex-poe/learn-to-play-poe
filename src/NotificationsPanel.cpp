#include "NotificationsPanel.h"

#include <QDateTime>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

NotificationsPanel::NotificationsPanel(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    m_container = new QWidget;
    m_layout = new QVBoxLayout(m_container);
    m_layout->setAlignment(Qt::AlignTop);
    m_layout->setSpacing(6);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->addStretch();

    setWidget(m_container);
}

void NotificationsPanel::addNotification(const QString &message, const NotificationStyle &style)
{
    insertCard({}, {}, message, QDateTime::currentDateTime().toString("HH:mm"), style);
}

void NotificationsPanel::addNotification(const QString &title, const QString &tag,
                                         const QString &message, const NotificationStyle &style)
{
    insertCard(title, tag, message, QDateTime::currentDateTime().toString("HH:mm"), style);
}

void NotificationsPanel::insertCard(const QString &title, const QString &tag,
                                    const QString &message, const QString &timestamp,
                                    const NotificationStyle &style)
{
    auto *notif = new NotificationWidget(title, tag, message, timestamp, style, m_container);
    m_layout->insertWidget(0, notif);

    QTimer::singleShot(0, this, [this]() {
        verticalScrollBar()->setValue(0);
    });
}
