// src/Database.h (C++)

#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <sqlite3.h>

class Database
{
public:
    explicit Database(const QString &path, bool readOnly = false);
    ~Database();

    bool isOpen() const { return m_db != nullptr; }
    QString lastError() const { return m_lastError; }
    QString path() const { return m_path; }

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
        QString accountName; // may be empty
        QString charName;    // may be empty
        QString charClass;   // may be empty
        QString installPath; // installs.path — which Client.txt this came from
    };

    // Returns whispers ordered by time; optionally filtered to one player.
    // limit > 0 returns only the most recent N messages; offset skips the newest N.
    QList<WhisperRecord> fetchWhispers(const QString &playerFilter = {}, int limit = 0, int offset = 0) const;

    // Returns distinct whisper partner names, ordered by most-recent message.
    QStringList fetchWhisperPartners() const;

    // Returns partners ordered by most-recent activity, each with their distinct
    // active dates (YYYY-MM-DD), most-recent first. Used for the filter menu buckets.
    QList<PartnerRecord> fetchWhisperPartnersWithDates() const;

    // Returns unified chat+DM records, most recent N rows in chronological order.
    // channels: which public channel prefixes to include ('#', '$', '%', '&').
    // includeDms: also include rows from the whispers table.
    // limit > 0 returns only the most recent N rows; offset skips the newest N.
    // fromDate/toDate: optional "YYYY-MM-DD" bounds (inclusive).
    QList<ChatRecord> fetchChats(const QSet<QChar> &channels, bool includeDms,
                                 int limit = 100,
                                 const QString &fromDate = {},
                                 const QString &toDate = {},
                                 int offset = 0) const;

    // Returns distinct dates ("YYYY-MM-DD") that have data for the given filter,
    // most-recent first. Used to populate the filter panel date buckets.
    QStringList fetchChatDates(const QSet<QChar> &channels, bool includeDms) const;

    // Returns sessions newest-first then reversed to ASC for display, with joined
    // account/char/install info. limit > 0 caps the result; offset skips the newest N.
    QList<SessionRecord> fetchSessions(int limit = 0, int offset = 0) const;

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

    // Returns game-start and game-stop events as a flat chronological list.
    // limit > 0 returns only the most recent N events; offset skips the newest N.
    QList<SessionEventRecord> fetchSessionEvents(int limit = 0, int offset = 0) const;

    struct ZoneTransitionRecord
    {
        QString areaName;    // display_name, or code if display_name is absent
        QString areaCode;    // areas.code
        QString areaType;    // areas.type (e.g. "Map", "Act 1"), or empty
        QString areaSubtype; // areas.subtype (e.g. "Town"), or empty
        int areaLevel{0};
        QString enteredAt;    // "YYYY-MM-DD HH:MM:SS"
        int durationSecs{-1}; // -1 when the span is still open (current zone)
    };

    // Returns zone transitions for the most recent session, newest first.
    // limit > 0 caps the result; offset skips that many rows (for pagination).
    QList<ZoneTransitionRecord> fetchZoneTransitions(int limit = 50, int offset = 0) const;

    // Returns zone transitions for a specific session by ID, newest first.
    QList<ZoneTransitionRecord> fetchZoneTransitions(int limit, int offset, qint64 sessionId) const;

    struct ClientScreenEventRecord
    {
        QString eventType;  // "login_screen" or "char_select"
        QString occurredAt; // "YYYY-MM-DD HH:MM:SS"
    };

    // Returns client-screen events (login_screen / char_select) for the most
    // recent open session, newest first.
    QList<ClientScreenEventRecord> fetchClientScreenEvents() const;

    // Returns client-screen events for a specific session by ID, newest first.
    QList<ClientScreenEventRecord> fetchClientScreenEvents(qint64 sessionId) const;

    struct AfkRecord
    {
        QString afkOnAt;      // "YYYY-MM-DD HH:MM:SS"
        QString afkOffAt;     // empty if the player is still AFK
        int     durationSecs{-1}; // computed; -1 if still open
    };

    // Returns AFK intervals for the most recent open session, newest first.
    // limit > 0 caps the result.
    QList<AfkRecord> fetchAfkRecords(int limit = 0) const;

    // Returns AFK intervals for a specific session by ID, newest first.
    QList<AfkRecord> fetchAfkRecords(int limit, qint64 sessionId) const;

    struct AltTabRecord
    {
        QString outAt;        // "YYYY-MM-DD HH:MM:SS"
        QString inAt;         // empty if still alt-tabbed out
        int     durationSecs{-1}; // computed; -1 if still out
    };

    // Returns alt-tab intervals for the most recent open session, newest first.
    // limit > 0 caps the result.
    QList<AltTabRecord> fetchAltTabRecords(int limit = 0) const;

    // Returns alt-tab intervals for a specific session by ID, newest first.
    QList<AltTabRecord> fetchAltTabRecords(int limit, qint64 sessionId) const;

private:
    void applyPragmas();

    // Arms the per-query budget timer. Call at the start of every fetch method
    // that runs on the UI thread. The progress handler interrupts any query whose
    // total elapsed time exceeds kQueryBudgetMs, returning SQLITE_INTERRUPT so
    // the call returns early with partial/empty results instead of blocking.
    void armQueryBudget() const;
    static int s_queryProgressHandler(void *ctx);
    static constexpr qint64 kQueryBudgetMs = 30;

    sqlite3 *m_db{nullptr};
    QString m_path;
    QString m_lastError;
    bool m_readOnly{false};
    mutable QElapsedTimer m_queryTimer; // invalid until first armQueryBudget() call
};
