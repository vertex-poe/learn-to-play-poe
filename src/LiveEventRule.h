#pragma once

#include <QString>
#include <QVariantMap>

// A "when X → do Y" rule evaluated against live game events.
//
// eventType  - must match LiveEvent::type; "" matches any event
// dataFilter - additional key=value constraints on LiveEvent::data (all must match)
// actionType - currently only "notify" (show in-app notification)
// actionParams for "notify": "title" and "message" template strings.
//   Template placeholders: {key} expands from LiveEvent::data; {timestamp} and {type} always available.
struct LiveEventRule {
    QString     id;           // unique string id
    QString     label;        // user-visible preset name, e.g. "Whisper from player"
    QString     eventType;    // "" = any
    QVariantMap dataFilter;   // optional: AND of field=value constraints
    QString     actionType;   // "notify"
    QVariantMap actionParams; // action-specific params
    bool        enabled{true};
};
