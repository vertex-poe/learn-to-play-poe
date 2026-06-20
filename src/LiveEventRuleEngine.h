#pragma once

#include "LiveEvent.h"
#include "LiveEventRule.h"

#include <QObject>
#include <QVector>

// Evaluates LiveEventRules against events from LiveEventBus and dispatches actions.
class LiveEventRuleEngine : public QObject
{
    Q_OBJECT
public:
    explicit LiveEventRuleEngine(QObject* parent = nullptr);

    void setRules(const QVector<LiveEventRule>& rules);
    const QVector<LiveEventRule>& rules() const { return m_rules; }

signals:
    void notifyRequested(const QString& title, const QString& tag, const QString& message);

private slots:
    void onEvent(const LiveEvent& event);

private:
    QString expandTemplate(const QString& tmpl, const LiveEvent& event) const;

    QVector<LiveEventRule> m_rules;
};
