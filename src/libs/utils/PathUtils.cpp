// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "utils/PathUtils.hpp"

#include <QtCore/QDir>

namespace Utils::PathUtils {

QString normalizePath(QStringView path)
{
    QString s = QDir::fromNativeSeparators(path.toString()).trimmed();
    QString cleaned = QDir::cleanPath(s);
    if (cleaned == ".")
        cleaned.clear();
    return cleaned;
}

QString cleanPath(QStringView path) {
    return normalizePath(path);
}

QString basename(QStringView path)
{
    const QString cleaned = normalizePath(path);
    if (cleaned.isEmpty())
        return {};

    const int slash = cleaned.lastIndexOf('/');
    if (slash < 0)
        return cleaned;
    if (slash == cleaned.size() - 1)
        return {};
    return cleaned.mid(slash + 1);
}

QString extension(QStringView path)
{
    const QString name = basename(path);
    const int dot = name.lastIndexOf('.');
    if (dot <= 0)
        return {};
    return name.mid(dot + 1);
}

QString stem(QStringView path)
{
    const QString name = basename(path);
    const int dot = name.lastIndexOf('.');
    if (dot <= 0)
        return name;
    return name.left(dot);
}

bool hasExtension(QStringView path, QStringView ext, Qt::CaseSensitivity cs)
{
    const QString current = extension(path);
    QString wanted = ext.toString();
    if (wanted.startsWith('.'))
        wanted.remove(0, 1);
    return QString::compare(current, wanted, cs) == 0;
}

QString ensureExtension(QStringView path, QStringView ext)
{
    QString result = path.toString();
    QString wanted = ext.toString();
    if (wanted.isEmpty())
        return result;
    if (wanted.startsWith('.'))
        wanted.remove(0, 1);
    if (hasExtension(result, wanted))
        return result;
    if (!result.isEmpty() && !result.endsWith('.'))
        result.append('.');
    result.append(wanted);
    return result;
}

QString sanitizeFileName(QStringView name)
{
    QString out;
    out.reserve(name.size());

    for (const QChar c : name) {
        if (c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u'.' || c == u' ') {
            out.append(c);
        } else {
            out.append(u'_');
        }
    }

    out = out.trimmed();
    while (out.endsWith('.'))
        out.chop(1);

    if (out.isEmpty())
        return QStringLiteral("untitled");
    return out;
}

} // namespace Utils::PathUtils
