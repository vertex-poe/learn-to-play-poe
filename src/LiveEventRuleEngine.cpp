#include "LiveEventRuleEngine.h"
#include "LiveEventBus.h"

#include <QRegularExpression>

LiveEventRuleEngine::LiveEventRuleEngine(QObject* parent)
    : QObject(parent)
{
    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &LiveEventRuleEngine::onEvent);
}

void LiveEventRuleEngine::setRules(const QVector<LiveEventRule>& rules)
{
    m_rules = rules;
}

void LiveEventRuleEngine::onEvent(const LiveEvent& event)
{
    for (const LiveEventRule& rule : m_rules) {
        if (!rule.enabled)
            continue;
        if (!rule.eventType.isEmpty() && rule.eventType != event.type)
            continue;

        bool filterMatch = true;
        for (auto it = rule.dataFilter.constBegin(); it != rule.dataFilter.constEnd(); ++it) {
            if (event.data.value(it.key()).toString() != it.value().toString()) {
                filterMatch = false;
                break;
            }
        }
        if (!filterMatch)
            continue;

        if (rule.actionType == QLatin1String("notify")) {
            QString title = expandTemplate(rule.actionParams.value("title").toString(), event);
            if (title.isEmpty())
                title = rule.label.isEmpty() ? event.type : rule.label;
            const QString message = expandTemplate(rule.actionParams.value("message").toString(), event);
            emit notifyRequested(title, QStringLiteral("alert"), message);
        }
    }
}

QString LiveEventRuleEngine::expandTemplate(const QString& tmpl, const LiveEvent& event) const
{
    QString result = tmpl;
    static const QRegularExpression placeholderRe(QStringLiteral(R"(\{(\w+)\})"));

    // Collect substitutions before replacing to avoid index shifts.
    QVector<std::pair<QString, QString>> subs;
    auto it = placeholderRe.globalMatch(result);
    while (it.hasNext()) {
        const auto m   = it.next();
        const QString key = m.captured(1);
        if (key == QLatin1String("timestamp"))
            subs.push_back({m.captured(0), event.timestamp});
        else if (key == QLatin1String("type"))
            subs.push_back({m.captured(0), event.type});
        else if (event.data.contains(key))
            subs.push_back({m.captured(0), event.data.value(key).toString()});
    }

    for (const auto& [from, to] : subs)
        result.replace(from, to);

    return result;
}
