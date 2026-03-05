// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/AieLogPanel.hpp"
#include "aieplugin/hlir_sync/AieOutputLog.hpp"

#include <utils/ui/SidebarPanelFrame.hpp>

#include <QtCore/QDateTime>
#include <QtGui/QTextCursor>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

namespace Aie::Internal {

AieLogPanel::AieLogPanel(AieOutputLog* log, QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* frame = new Utils::SidebarPanelFrame(this);
    frame->setTitle(QStringLiteral("Log"));
    frame->setSearchEnabled(false);
    frame->setHeaderDividerVisible(true);
    frame->addAction(QStringLiteral("clear"),
                     QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton),
                     QStringLiteral("Clear log"));

    m_log = new QTextEdit(frame);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QTextEdit::WidgetWidth);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setObjectName(QStringLiteral("AieLogPanelTextEdit"));

    frame->setContentWidget(m_log);
    root->addWidget(frame);

    connect(frame, &Utils::SidebarPanelFrame::actionTriggered, this,
            [this](const QString& id) {
                if (id == QLatin1StringView("clear")) {
                    m_log->clear();
                    m_liveAnchor = -1;
                    m_liveSteps.clear();
                    m_liveTimestamp.clear();
                }
            });

    if (!log)
        return;

    // Replay finalized entries that arrived before this panel was created.
    for (const auto& entry : log->entries())
        appendEntry(entry.success, entry.message);

    connect(log, &AieOutputLog::entryAdded, this, &AieLogPanel::appendEntry);
    connect(log, &AieOutputLog::runStarted, this, &AieLogPanel::onRunStarted);
    connect(log, &AieOutputLog::runStepAppended, this, &AieLogPanel::onRunStepAppended);
    connect(log, &AieOutputLog::runFinalized, this, &AieLogPanel::onRunFinalized);
}

void AieLogPanel::appendEntry(bool success, const QString& message)
{
    const QString time  = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString color = success ? QStringLiteral("#4CAF50") : QStringLiteral("#F44336");
    const QString icon  = success ? QStringLiteral("\u2713") : QStringLiteral("\u2717");

    const QString html =
        QStringLiteral("<p style='margin:6px 0 2px 0;'>"
                       "<span style='color:%1;font-weight:bold;'>%2 [%3]</span>"
                       "</p>"
                       "<p style='margin:0 0 8px 0;"
                       "white-space:pre-wrap;"
                       "color:#cccccc;"
                       "font-family:monospace;"
                       "font-size:11px;'>%4</p>")
            .arg(color, icon, time,
                 message.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>")));

    m_log->append(html);
    scrollToEnd();
}

void AieLogPanel::onRunStarted()
{
    m_liveSteps.clear();
    m_liveTimestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));

    // Ensure the live block starts on its own paragraph, then record the anchor.
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    if (cursor.position() > 0)
        cursor.insertBlock();
    m_liveAnchor = cursor.position();
    cursor.insertHtml(buildLiveHtml());
    scrollToEnd();
}

void AieLogPanel::onRunStepAppended(bool ok, const QString& label)
{
    m_liveSteps.append({ok, label});
    updateLiveBlock();
}

void AieLogPanel::onRunFinalized(bool ok, const QString& summary)
{
    if (m_liveAnchor >= 0) {
        QTextCursor cursor(m_log->document());
        cursor.setPosition(m_liveAnchor);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.insertHtml(buildFinalHtml(ok, summary));
    } else {
        appendEntry(ok, summary);
    }

    m_liveAnchor = -1;
    m_liveSteps.clear();
    m_liveTimestamp.clear();
    scrollToEnd();
}

void AieLogPanel::updateLiveBlock()
{
    if (m_liveAnchor < 0)
        return;

    QTextCursor cursor(m_log->document());
    cursor.setPosition(m_liveAnchor);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertHtml(buildLiveHtml());
    scrollToEnd();
}

QString AieLogPanel::buildLiveHtml() const
{
    // Gray bullet header while the run is in progress.
    QString html = QStringLiteral(
        "<p style='margin:6px 0 4px 0;'>"
        "<span style='color:#888888;font-weight:bold;'>\u25cf [%1]</span>"
        "</p>").arg(m_liveTimestamp);

    for (const auto& step : m_liveSteps) {
        const QString color = step.ok ? QStringLiteral("#4CAF50") : QStringLiteral("#F44336");
        const QString icon  = step.ok ? QStringLiteral("\u2713") : QStringLiteral("\u2717");
        html += QStringLiteral(
            "<p style='margin:0 0 2px 0;"
            "color:#cccccc;"
            "font-family:monospace;"
            "font-size:11px;'>"
            "<span style='color:%1;'>%2</span> %3</p>")
            .arg(color, icon, step.label.toHtmlEscaped());
    }
    return html;
}

QString AieLogPanel::buildFinalHtml(bool ok, const QString& summary) const
{
    const QString color = ok ? QStringLiteral("#4CAF50") : QStringLiteral("#F44336");
    const QString icon  = ok ? QStringLiteral("\u2713") : QStringLiteral("\u2717");

    // Header in final color, matching the appendEntry style.
    QString html = QStringLiteral(
        "<p style='margin:6px 0 2px 0;'>"
        "<span style='color:%1;font-weight:bold;'>%2 [%3]</span>"
        "</p>").arg(color, icon, m_liveTimestamp);

    for (const auto& step : m_liveSteps) {
        const QString sc = step.ok ? QStringLiteral("#4CAF50") : QStringLiteral("#F44336");
        const QString si = step.ok ? QStringLiteral("\u2713") : QStringLiteral("\u2717");
        html += QStringLiteral(
            "<p style='margin:0 0 2px 0;"
            "color:#cccccc;"
            "font-family:monospace;"
            "font-size:11px;'>"
            "<span style='color:%1;'>%2</span> %3</p>")
            .arg(sc, si, step.label.toHtmlEscaped());
    }

    if (!summary.isEmpty()) {
        html += QStringLiteral(
            "<p style='margin:4px 0 8px 0;"
            "white-space:pre-wrap;"
            "color:#cccccc;"
            "font-family:monospace;"
            "font-size:11px;'>%1</p>")
            .arg(summary.toHtmlEscaped().replace(u'\n', QStringLiteral("<br>")));
    }

    return html;
}

void AieLogPanel::scrollToEnd()
{
    QTextCursor cursor = m_log->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_log->setTextCursor(cursor);
}

} // namespace Aie::Internal
