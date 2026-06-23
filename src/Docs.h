#pragma once

#include <QString>

struct DocSource {
    QString label;
    QString url;
};

inline DocSource docSource(const char *label, const char *path)
{
    return {label,
            QStringLiteral("https://vertex-poe1.github.io/learn-to-play-poe1/") + path};
}
