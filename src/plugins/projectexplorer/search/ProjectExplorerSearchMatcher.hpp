// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"

#include <QtCore/Qt>
#include <QtCore/QString>

namespace ProjectExplorer::Internal {

struct ProjectExplorerMatchResult {
    bool matched = false;
    int start = -1;
    int length = 0;
};

class PROJECTEXPLORER_EXPORT ProjectExplorerSearchMatcher
{
public:
    static ProjectExplorerMatchResult match(const QString& text,
                                            const QString& query,
                                            Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive);
};

} // namespace ProjectExplorer::Internal
