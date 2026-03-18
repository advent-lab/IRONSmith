// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

class QTextEdit;

namespace Aie::Internal {

class AieOutputLog;

class AieLogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit AieLogPanel(AieOutputLog* log, QWidget* parent = nullptr);
    void setLog(AieOutputLog* log);

private:
    void appendEntry(bool success, const QString& message);
    void onRunStarted();
    void onRunStepAppended(bool ok, const QString& label);
    void onRunFinalized(bool ok, const QString& summary);
    void updateLiveBlock();
    QString buildLiveHtml() const;
    QString buildFinalHtml(bool ok, const QString& summary) const;
    void scrollToEnd();

    void clearDisplay();

    AieOutputLog* m_outputLog = nullptr;
    QTextEdit*    m_log       = nullptr;

    // Live run state — one block per button press, updated in-place.
    struct LiveStep { bool ok; QString label; };
    QString         m_liveTimestamp;
    QList<LiveStep> m_liveSteps;
    int             m_liveAnchor = -1; // character position of live block start
};

} // namespace Aie::Internal
