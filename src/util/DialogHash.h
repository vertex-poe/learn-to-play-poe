#pragma once

#include <QCryptographicHash>
#include <QString>

// Canonical hash for NPC dialog text.  Every code path that hashes dialog —
// the CLI, the in-app form, and LogIngestWorker — must call this function so
// that normalization is identical and hashes are interchangeable.
//
// Contract: NFC-normalise → trim whitespace → UTF-8 → SHA-256 → first 16 hex chars.
inline QString dialogHash(const QString &text)
{
    const QByteArray utf8 =
        text.normalized(QString::NormalizationForm_C).trimmed().toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(utf8, QCryptographicHash::Sha256).toHex()
    ).left(16);
}
