#pragma once

#include "ui/widgets/NotificationWidget.h"

#include <QScrollArea>

class QVBoxLayout;

class NotificationsPanel : public QScrollArea
{
    Q_OBJECT
public:
    explicit NotificationsPanel(QWidget *parent = nullptr);
    void addNotification(const QString &message, const NotificationStyle &style = {});
    void addNotification(const QString &title, const QString &tag,
                         const QString &message, const NotificationStyle &style = {});

private:
    void insertCard(const QString &title, const QString &tag,
                    const QString &message, const QString &timestamp,
                    const NotificationStyle &style);

    QWidget     *m_container{};
    QVBoxLayout *m_layout{};
};
