#pragma once

#include <QString>

struct DocSource {
    QString label;
    QString url;
};

inline DocSource docSource(const char *label, const char *path)
{
    return {label,
            QStringLiteral("https://vertex-poe.github.io/learn-to-play-poe/") + path};
}
