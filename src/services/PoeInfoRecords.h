// src/services/PoeInfoRecords.h (C++)

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Plain data-transfer types matching the shapes poe-info-service's WebSocket
// API (chat.messages, dm.messages, log.sessions, log.session, ...) returns —
// the C++ side's picture of that contract. Shared by pages that render this
// data (ChatPage, DmPage, LogPage, SessionViewPage) via PoeInfoClient.
// poe-info-service owns the database exclusively (ADR-006); nothing here
// performs any I/O, these are just parse targets for JSON responses.
namespace Records
{

struct WhisperRecord
{
    QString direction; // "from" or "to"
    QString playerName;
    QString guildTag; // may be empty
    QString message;
    QString occurredAt; // "YYYY-MM-DD HH:MM:SS"
};

struct PartnerRecord
{
    QString name;
    QStringList dates; // distinct "YYYY-MM-DD" values, most-recent first
};

struct ChatRecord
{
    QString source;  // "chat" or "dm"
    QString channel; // "#", "$", "%", "&", "@from", "@to"
    QString playerName;
    QString guildTag; // may be empty
    QString message;
    QString occurredAt; // "YYYY-MM-DD HH:MM:SS"
};

struct SessionRecord
{
    qint64 id{-1};
    QString startedAt;   // "YYYY-MM-DD HH:MM:SS"
    QString endedAt;     // empty if session is still open
    int totalSecs{-1};
    int activeSecs{-1};
    int afkSecs{0};      // time away (AFK timeout or alt-tab, merged); activeSecs already excludes it
    QString accountName; // may be empty
    QString charName;    // may be empty
    QString charClass;   // may be empty
    QString installPath; // installs.path — which Client.txt this came from
};

struct SessionEventRecord
{
    QString eventType;   // "start" or "stop"
    QString occurredAt;  // "YYYY-MM-DD HH:MM:SS"
    QString charName;    // may be empty
    QString charClass;   // may be empty
    QString installPath; // installs.path — which Client.txt this came from
    int activeSecs{-1};
    int totalSecs{-1};
};

struct ZoneTransitionRecord
{
    QString areaName;    // display_name, or code if display_name is absent
    QString areaCode;    // areas.code
    QString areaType;    // areas.type (e.g. "Map", "Act 1"), or empty
    QString areaSubtype; // areas.subtype (e.g. "Town"), or empty
    int areaLevel{0};
    QString enteredAt;    // "YYYY-MM-DD HH:MM:SS"
    int durationSecs{-1}; // -1 when the span is still open (current zone)
    int afkSecs{0};       // sum of away intervals (AFK timeout or alt-tab, merged) closed within this span
    QString afkOpenSince; // afk_on_at of a still-open away interval in this span, or empty if none
};

struct ClientScreenEventRecord
{
    QString eventType;  // "login_screen" or "char_select"
    QString occurredAt; // "YYYY-MM-DD HH:MM:SS"
};

// Mirrors proto.LeagueSummary — one entry of poe.leagues.list's response, or
// poe.leagues.detail's/poe.league's single result.
struct LeagueSummary
{
    QString name;
    QString realm;
    QString url;
    QString startAt;
    QString endAt;         // empty = permanent league
    QString description;
    QStringList rules;     // flattened rule id strings, e.g. "Hardcore"
    bool event{false};
    bool delveEvent{false};
};

} // namespace Records
