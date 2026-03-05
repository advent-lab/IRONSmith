// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>

namespace Aie::Internal {

/// Accumulates verification and code-generation result messages.
/// Owned by AiePlugin; AieLogPanel subscribes to it and replays history on construction.
class AieOutputLog : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        bool    success = false;
        QString message;
    };

    explicit AieOutputLog(QObject* parent = nullptr) : QObject(parent) {}

    /// Emit a standalone log entry (e.g. "No design is open").
    void addEntry(bool success, const QString& message)
    {
        m_entries.append({success, message});
        emit entryAdded(success, message);
    }

    /// Begin a new timestamped run block (called once per button press).
    void startRun() { emit runStarted(); }

    /// Append one step result to the current run block.
    void appendRunStep(bool ok, const QString& label) { emit runStepAppended(ok, label); }

    /// Finalize the current run block with a result summary.
    void finalizeRun(bool ok, const QString& summary)
    {
        m_entries.append({ok, summary});
        emit runFinalized(ok, summary);
    }

    const QList<Entry>& entries() const { return m_entries; }

signals:
    void entryAdded(bool success, const QString& message);
    void runStarted();
    void runStepAppended(bool ok, const QString& label);
    void runFinalized(bool ok, const QString& summary);

private:
    QList<Entry> m_entries;
};

} // namespace Aie::Internal
