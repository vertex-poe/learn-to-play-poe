#pragma once

#include "LiveEventRule.h"

#include <QDialog>
#include <QVector>

class QListWidget;

class LiveAlertsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LiveAlertsDialog(const QVector<LiveEventRule>& rules, QWidget* parent = nullptr);

    QVector<LiveEventRule> rules() const { return m_rules; }

private slots:
    void addRule();
    void editRule();
    void removeRule();

private:
    void rebuildList();
    QString ruleDescription(const LiveEventRule& rule) const;
    bool editRuleDialog(LiveEventRule& rule);

    QVector<LiveEventRule> m_rules;
    QListWidget*           m_list{};
};
