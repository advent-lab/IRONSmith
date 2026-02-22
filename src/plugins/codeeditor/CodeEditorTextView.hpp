// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/style/CodeEditorStyleManager.hpp"

#include <QtCore/QString>
#include <QtWidgets/QWidget>
#include <qnamespace.h>

QT_BEGIN_NAMESPACE
class QEvent;
class QLabel;
class QPlainTextEdit;
class QWheelEvent;
class QResizeEvent;
QT_END_NAMESPACE

#if CODEEDITOR_HAVE_QSCI
class QsciScintilla;
#endif

namespace CodeEditor {

class CodeEditorTextView final : public QWidget {
    Q_OBJECT
public:
    explicit CodeEditorTextView(QWidget* parent = nullptr);

    void setReadOnlyMode(bool readOnly);
    bool readOnlyMode() const;

    void setLanguageId(const QString& languageId);
    QString languageId() const;

    void setPathHint(const QString& path);
    void setText(const QString& text);
    QString text() const;
    void appendText(const QString& text);

    void zoomInEditor(int steps = 1);
    void zoomOutEditor(int steps = 1);
    void resetZoom();
    void setZoomLevel(int level);
    int zoomLevel() const noexcept;

signals:
    void textEdited();
    void zoomLevelChanged(int level);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void handleNativeTextChanged();
    void applyZoomDelta(int steps);
    void handleWheelScroll(QWheelEvent* wheelEvent);
    void updateReaderModeBadgeGeometry();
    void syncReaderModeBadgeVisibility();

#if CODEEDITOR_HAVE_QSCI
    void configureScintilla();
    void applyLexer(const QString& languageId);
    QsciScintilla* m_scintilla = nullptr;
#else
    QPlainTextEdit* m_plain = nullptr;
#endif

    Style::CodeEditorStyleManager m_styleManager;
    QLabel* m_readerModeBadge = nullptr;
    bool m_blockTextSignal = false;
    QString m_languageId;
    int m_zoomLevel = 0;
    qreal m_pendingScrollX = 0.0;
    qreal m_pendingScrollY = 0.0;
};
} // namespace CodeEditor
