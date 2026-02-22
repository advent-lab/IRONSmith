// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/qtypes.h>
#include <QtCore/QSet>
#include <QtCore/QString>

namespace CodeEditor::Constants {
constexpr inline char kSidebarToolId[] = "IRONSmith.CodeEditor";

constexpr inline quint64 kSessionOpenMaxBytes = 32 * 1024 * 1024;
constexpr inline quint64 kQuickViewMaxBytes = 2 * 1024 * 1024;

constexpr inline char kLanguageIdC[] = "c";
constexpr inline char kLanguageIdCpp[] = "cpp";
constexpr inline char kLanguageIdPython[] = "python";
constexpr inline char kLanguageIdJson[] = "json";
constexpr inline char kLanguageIdXml[] = "xml";
constexpr inline char kLanguageIdText[] = "text";

inline const QSet<QString> kSupportedLanguages = {
    kLanguageIdC,
    kLanguageIdCpp,
    kLanguageIdPython,
    kLanguageIdJson,
    kLanguageIdXml,
    kLanguageIdText,
};

} // namespace CodeEditor::Constants
