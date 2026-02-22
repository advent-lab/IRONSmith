// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/CodeEditorTextView.hpp"

#include <utils/Comparisons.hpp>

#include <QtCore/QEvent>
#include <QtGui/QColor>
#include <QtGui/QFontDatabase>
#include <QtGui/QResizeEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPlainTextEdit>

#include <algorithm>

#if CODEEDITOR_HAVE_QSCI
#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerxml.h>
#endif

namespace CodeEditor {

namespace {

constexpr qreal kWheelPixelsPerStep = 8.0;
constexpr int kMinZoomLevel = -8;
constexpr int kMaxZoomLevel = 24;
constexpr int kReaderModeBadgeInset = 10;

int consumeWholePixels(qreal& pending)
{
    if (pending < 1.0 && pending > -1.0)
        return 0;

    const int whole = static_cast<int>(pending);
    pending -= static_cast<qreal>(whole);
    return whole;
}

} // namespace

CodeEditorTextView::CodeEditorTextView(QWidget* parent)
    : QWidget(parent)
    , m_styleManager(Style::CodeEditorStyleManager::loadDefault())
{
    setObjectName(QStringLiteral("CodeEditorTextView"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if CODEEDITOR_HAVE_QSCI
    m_scintilla = new QsciScintilla(this);
    m_scintilla->setObjectName(QStringLiteral("CodeEditorScintilla"));
    m_scintilla->installEventFilter(this);
    if (m_scintilla->viewport())
        m_scintilla->viewport()->installEventFilter(this);
    layout->addWidget(m_scintilla);
    configureScintilla();
    connect(m_scintilla, &QsciScintilla::textChanged,
            this, &CodeEditorTextView::handleNativeTextChanged);
#else
    m_plain = new QPlainTextEdit(this);
    m_plain->setObjectName(QStringLiteral("CodeEditorPlainText"));
    m_plain->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_plain->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_plain->installEventFilter(this);
    if (m_plain->viewport())
        m_plain->viewport()->installEventFilter(this);
    if (QScrollBar* horizontalBar = m_plain->horizontalScrollBar()) {
        horizontalBar->setSingleStep(1);
        horizontalBar->setPageStep(20);
    }
    if (QScrollBar* verticalBar = m_plain->verticalScrollBar())
        verticalBar->setSingleStep(1);
    m_styleManager.applyEditorView(m_plain);
    layout->addWidget(m_plain);
    connect(m_plain, &QPlainTextEdit::textChanged,
            this, &CodeEditorTextView::handleNativeTextChanged);
#endif

    m_readerModeBadge = new QLabel(QStringLiteral("Reader Mode"), this);
    m_readerModeBadge->setObjectName(QStringLiteral("CodeEditorReaderModeBadge"));
    m_readerModeBadge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_readerModeBadge->setVisible(false);
    m_readerModeBadge->raise();
    updateReaderModeBadgeGeometry();
    syncReaderModeBadgeVisibility();
}

void CodeEditorTextView::setReadOnlyMode(bool readOnly)
{
#if CODEEDITOR_HAVE_QSCI
    if (m_scintilla)
        m_scintilla->setReadOnly(readOnly);
#else
    if (m_plain)
        m_plain->setReadOnly(readOnly);
#endif
    syncReaderModeBadgeVisibility();
}

bool CodeEditorTextView::readOnlyMode() const
{
#if CODEEDITOR_HAVE_QSCI
    return m_scintilla ? m_scintilla->isReadOnly() : true;
#else
    return m_plain ? m_plain->isReadOnly() : true;
#endif
}

void CodeEditorTextView::setLanguageId(const QString& languageId)
{
    m_languageId = languageId.trimmed().toLower();

#if CODEEDITOR_HAVE_QSCI
    applyLexer(m_languageId);
#endif
}

QString CodeEditorTextView::languageId() const
{
    return m_languageId;
}

void CodeEditorTextView::setPathHint(const QString& path)
{
    setWindowFilePath(path.trimmed());
}

void CodeEditorTextView::setText(const QString& text)
{
    m_blockTextSignal = true;
#if CODEEDITOR_HAVE_QSCI
    if (m_scintilla)
        m_scintilla->setText(text);
#else
    if (m_plain)
        m_plain->setPlainText(text);
#endif
    m_blockTextSignal = false;
}

QString CodeEditorTextView::text() const
{
#if CODEEDITOR_HAVE_QSCI
    return m_scintilla ? m_scintilla->text() : QString();
#else
    return m_plain ? m_plain->toPlainText() : QString();
#endif
}

void CodeEditorTextView::appendText(const QString& text)
{
#if CODEEDITOR_HAVE_QSCI
    if (m_scintilla)
        m_scintilla->append(text);
#else
    if (m_plain)
        m_plain->appendPlainText(text);
#endif
}

void CodeEditorTextView::zoomInEditor(int steps)
{
    applyZoomDelta(steps > 0 ? steps : 1);
}

void CodeEditorTextView::zoomOutEditor(int steps)
{
    applyZoomDelta(steps > 0 ? -steps : -1);
}

void CodeEditorTextView::resetZoom()
{
    setZoomLevel(0);
}

void CodeEditorTextView::setZoomLevel(int level)
{
    const int nextZoom = std::clamp(level, kMinZoomLevel, kMaxZoomLevel);
    const int delta = nextZoom - m_zoomLevel;
    if (delta == 0)
        return;

#if CODEEDITOR_HAVE_QSCI
    if (m_scintilla) {
        if (delta > 0)
            m_scintilla->zoomIn(delta);
        else
            m_scintilla->zoomOut(-delta);
    }
#else
    if (m_plain) {
        if (delta > 0)
            m_plain->zoomIn(delta);
        else
            m_plain->zoomOut(-delta);
    }
#endif

    m_zoomLevel = nextZoom;
    emit zoomLevelChanged(m_zoomLevel);
}

int CodeEditorTextView::zoomLevel() const noexcept
{
    return m_zoomLevel;
}

void CodeEditorTextView::handleNativeTextChanged()
{
    if (m_blockTextSignal)
        return;

    emit textEdited();
}

void CodeEditorTextView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateReaderModeBadgeGeometry();
}

bool CodeEditorTextView::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel)
        return QWidget::eventFilter(watched, event);

#if CODEEDITOR_HAVE_QSCI
    const bool isEditorTarget = m_scintilla
                                && (watched == m_scintilla || watched == m_scintilla->viewport());
#else
    const bool isEditorTarget = m_plain
                                && (watched == m_plain || watched == m_plain->viewport());
#endif
    if (!isEditorTarget)
        return QWidget::eventFilter(watched, event);

    auto* wheelEvent = static_cast<QWheelEvent*>(event);
    const Qt::KeyboardModifiers modifiers = wheelEvent->modifiers();
    const bool isZoomGesture = modifiers.testFlag(Qt::ControlModifier)
                               || modifiers.testFlag(Qt::MetaModifier);
    if (isZoomGesture) {
        const QPoint pixelDelta = wheelEvent->pixelDelta();
        const QPoint angleDelta = wheelEvent->angleDelta();
        const int deltaY = !pixelDelta.isNull() ? pixelDelta.y() : angleDelta.y();
        if (deltaY > 0)
            zoomInEditor();
        else if (deltaY < 0)
            zoomOutEditor();

        wheelEvent->accept();
        return true;
    }

    handleWheelScroll(wheelEvent);
    return wheelEvent->isAccepted();
}

void CodeEditorTextView::handleWheelScroll(QWheelEvent* wheelEvent)
{
    QScrollBar* horizontalBar = nullptr;
    QScrollBar* verticalBar = nullptr;

#if CODEEDITOR_HAVE_QSCI
    if (m_scintilla) {
        horizontalBar = m_scintilla->horizontalScrollBar();
        verticalBar = m_scintilla->verticalScrollBar();
    }
#else
    if (m_plain) {
        horizontalBar = m_plain->horizontalScrollBar();
        verticalBar = m_plain->verticalScrollBar();
    }
#endif

    if (!horizontalBar || !verticalBar) {
        wheelEvent->ignore();
        return;
    }

    QPointF pixelDelta = wheelEvent->pixelDelta();
    if (pixelDelta.isNull()) {
        const QPoint angleDelta = wheelEvent->angleDelta();
        pixelDelta.setX((static_cast<qreal>(angleDelta.x()) / 120.0) * kWheelPixelsPerStep);
        pixelDelta.setY((static_cast<qreal>(angleDelta.y()) / 120.0) * kWheelPixelsPerStep);
    }

    m_pendingScrollX += pixelDelta.x();
    m_pendingScrollY += pixelDelta.y();

    const int scrollX = consumeWholePixels(m_pendingScrollX);
    const int scrollY = consumeWholePixels(m_pendingScrollY);

    if (scrollX != 0)
        horizontalBar->setValue(horizontalBar->value() - scrollX);
    if (scrollY != 0)
        verticalBar->setValue(verticalBar->value() - scrollY);

    wheelEvent->accept();
}

void CodeEditorTextView::applyZoomDelta(int steps)
{
    if (steps == 0)
        return;

    setZoomLevel(m_zoomLevel + steps);
}

void CodeEditorTextView::updateReaderModeBadgeGeometry()
{
    if (!m_readerModeBadge)
        return;

    m_readerModeBadge->adjustSize();
    const QSize badgeSize = m_readerModeBadge->sizeHint();
    const int x = std::max(kReaderModeBadgeInset,
                           width() - badgeSize.width() - kReaderModeBadgeInset);
    const int y = kReaderModeBadgeInset;
    m_readerModeBadge->setGeometry(x, y, badgeSize.width(), badgeSize.height());
}

void CodeEditorTextView::syncReaderModeBadgeVisibility()
{
    if (!m_readerModeBadge)
        return;

    const bool shouldShow = readOnlyMode();
    m_readerModeBadge->setVisible(shouldShow);
    if (shouldShow) {
        updateReaderModeBadgeGeometry();
        m_readerModeBadge->raise();
    }
}

#if CODEEDITOR_HAVE_QSCI
void CodeEditorTextView::configureScintilla()
{
    if (!m_scintilla)
        return;

    m_scintilla->setFrameShape(QFrame::NoFrame);
    m_scintilla->setStyleSheet(QString());
    m_scintilla->setUtf8(true);
    m_scintilla->setAutoIndent(true);
    m_scintilla->setIndentationGuides(false);
    m_scintilla->setIndentationsUseTabs(false);
    m_scintilla->setTabWidth(4);
    m_scintilla->setBraceMatching(QsciScintilla::SloppyBraceMatch);
    m_scintilla->setFolding(QsciScintilla::NoFoldStyle, 2);
    m_scintilla->setWrapMode(QsciScintilla::WrapNone);
    m_scintilla->setScrollWidthTracking(true);
    m_scintilla->setScrollWidth(1);
    m_scintilla->setCaretLineVisible(true);

    m_scintilla->setMarginType(0, QsciScintilla::NumberMargin);
    m_scintilla->setMarginLineNumbers(0, true);
    m_scintilla->setMarginWidth(0, QStringLiteral("00000"));
    m_scintilla->setMarginType(1, QsciScintilla::SymbolMargin);
    m_scintilla->setMarginWidth(1, 0);
    m_scintilla->setMarginType(2, QsciScintilla::SymbolMargin);
    m_scintilla->setMarginWidth(2, 0);
    m_scintilla->setMarginSensitivity(2, false);

    m_scintilla->setAutoCompletionSource(QsciScintilla::AcsDocument);
    m_scintilla->setAutoCompletionThreshold(2);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_scintilla->setFont(mono);
    m_scintilla->setMarginsFont(mono);

    if (QScrollBar* horizontalBar = m_scintilla->horizontalScrollBar()) {
        horizontalBar->setSingleStep(1);
        horizontalBar->setPageStep(20);
    }
    if (QScrollBar* verticalBar = m_scintilla->verticalScrollBar())
        verticalBar->setSingleStep(1);

    m_styleManager.applyEditorView(m_scintilla);
}

void CodeEditorTextView::applyLexer(const QString& languageId)
{
    if (!m_scintilla)
        return;

    if (auto old = m_scintilla->lexer()) {
        if (old->parent() == m_scintilla)
            old->deleteLater();
    }

    QsciLexer* lexer = nullptr;
    if (Utils::isOneOf(languageId, "c", "cpp", "json"))
        lexer = new QsciLexerCPP(m_scintilla);
    else if (languageId == QStringLiteral("python"))
        lexer = new QsciLexerPython(m_scintilla);
    else if (languageId == QStringLiteral("xml"))
        lexer = new QsciLexerXML(m_scintilla);

    if (lexer) {
        lexer->setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_styleManager.applyLexer(lexer, languageId);
    }

    m_scintilla->setLexer(lexer);
}
#endif

} // namespace CodeEditor
