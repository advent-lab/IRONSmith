// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "projectexplorer/ProjectExplorerGlobal.hpp"
#include "projectexplorer/api/ProjectExplorerTypes.hpp"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <atomic>

namespace ProjectExplorer::Internal {

class PROJECTEXPLORER_EXPORT ProjectExplorerSearchIndex final : public QObject
{
    Q_OBJECT

public:
    struct Entry final {
        QString nameLower;
        QString path;
    };

    explicit ProjectExplorerSearchIndex(QObject* parent = nullptr);

    void setEntries(const ProjectExplorer::ProjectEntryList& entries);
    QVector<QString> findMatches(const QString& query) const;
    bool isReady() const;

signals:
    void indexRebuilt();

private:
    QVector<Entry> m_entries;
    std::atomic<quint64> m_generation{0};
    bool m_ready = false;
};

} // namespace ProjectExplorer::Internal
