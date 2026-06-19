#pragma once

#include <QString>
#include <sqlite3.h>

class Database
{
public:
    explicit Database(const QString &path);
    ~Database();

    bool    isOpen()    const { return m_db != nullptr; }
    QString lastError() const { return m_lastError; }
    QString path()      const { return m_path; }

    struct InstallState {
        qint64 id{-1};
        qint64 fileCreatedAt{0};
        qint64 fileModifiedAt{0};
        qint64 fileSize{0};
        qint64 lastByteOffset{0};
    };

    // Inserts the install path if new; returns current state either way.
    InstallState upsertInstall(const QString &installPath);

private:
    void applyPragmas();
    void initSchema();

    sqlite3 *m_db{nullptr};
    QString  m_path;
    QString  m_lastError;
};
