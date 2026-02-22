// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/CodeEditorGlobal.hpp"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtGui/QColor>

QT_BEGIN_NAMESPACE
class QPlainTextEdit;
QT_END_NAMESPACE

#if CODEEDITOR_HAVE_QSCI
class QsciLexer;
class QsciScintilla;
#endif

namespace CodeEditor::Style {

class CODEEDITOR_EXPORT CodeEditorStyleManager final
{
public:
    struct StyleRule final {
        int styleId = -1;
        QColor foreground;
        QColor background;
        bool hasForeground = false;
        bool hasBackground = false;
    };

    struct SurfaceColors final {
        QColor paper;
        QColor text;
        QColor selectionBackground;
        QColor selectionForeground;
        QColor caret;
        QColor caretLine;
        QColor marginBaseBackground;
        QColor marginBaseForeground;
        QColor lineNumberBackground;
        QColor lineNumberForeground;
        QColor foldMarkerForeground;
    };

    static CodeEditorStyleManager loadDefault();
    static CodeEditorStyleManager loadFromJsonFile(const QString& filePath);

    const SurfaceColors& surfaceColors() const noexcept;
    bool hasLanguagePalette(QStringView languageId) const;
    int resolvedStyleCount(QStringView languageId) const;

    void applyEditorView(QPlainTextEdit* editor) const;

#if CODEEDITOR_HAVE_QSCI
    void applyEditorView(QsciScintilla* editor) const;
    void applyLexer(QsciLexer* lexer, QStringView languageId) const;
#endif

private:
    struct LanguagePalette final {
        QVector<StyleRule> styles;
    };

    static CodeEditorStyleManager buildFallback();
    static CodeEditorStyleManager loadFromJsonObject(const QJsonObject& root,
                                                     const QString& sourceLabel);
    static QString normalizeLanguageId(QStringView languageId);
    static QColor parseColor(const QString& value);
    static bool readColorField(const QJsonObject& object,
                               const QString& key,
                               QColor& inOutColor,
                               QStringList& errors,
                               const QString& context);
    static void readLanguagePalette(const QString& languageId,
                                    const QJsonObject& languageObject,
                                    CodeEditorStyleManager& inOutScheme,
                                    QStringList& errors);

    const LanguagePalette* resolvePalette(QStringView languageId) const;

    SurfaceColors m_surface;
    QHash<QString, LanguagePalette> m_languagePalettes;
    QHash<QString, QString> m_languageFallbacks;
};

} // namespace CodeEditor::Style