// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "codeeditor/style/CodeEditorStyleManager.hpp"

#include "codeeditor/CodeEditorGlobal.hpp"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QSet>
#include <QtCore/QtGlobal>
#include <QtGui/QPalette>
#include <QtWidgets/QFrame>
#include <QtWidgets/QPlainTextEdit>

#if CODEEDITOR_HAVE_QSCI
#include <Qsci/qscilexer.h>
#include <Qsci/qsciscintilla.h>
#endif

#include <algorithm>

namespace CodeEditor::Style {

namespace {

const QString kDefaultSchemeResourcePath = QStringLiteral(":/codeeditor/styles/default_dark.json");

CodeEditorStyleManager::StyleRule makeRule(int styleId, const QString& foreground)
{
    CodeEditorStyleManager::StyleRule rule;
    rule.styleId = styleId;
    rule.foreground = QColor(foreground);
    rule.hasForeground = rule.foreground.isValid();
    return rule;
}

} // namespace

CodeEditorStyleManager CodeEditorStyleManager::loadDefault()
{
    return loadFromJsonFile(kDefaultSchemeResourcePath);
}

CodeEditorStyleManager CodeEditorStyleManager::loadFromJsonFile(const QString& filePath)
{
    CodeEditorStyleManager scheme = buildFallback();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(ceditorlog) << "CodeEditorStyleManager: unable to open color scheme:"
                              << filePath;
        return scheme;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(ceditorlog).noquote()
            << QStringLiteral("CodeEditorStyleManager: invalid color scheme JSON '%1': %2")
                   .arg(filePath, parseError.errorString());
        return scheme;
    }

    return loadFromJsonObject(document.object(), filePath);
}

CodeEditorStyleManager CodeEditorStyleManager::loadFromJsonObject(const QJsonObject& root,
                                                                  const QString& sourceLabel)
{
    CodeEditorStyleManager scheme = buildFallback();
    QStringList errors;

    if (const QJsonValue surfaceValue = root.value(QStringLiteral("surface")); surfaceValue.isObject()) {
        const QJsonObject surfaceObject = surfaceValue.toObject();
        readColorField(surfaceObject,
                       QStringLiteral("paper"),
                       scheme.m_surface.paper,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("text"),
                       scheme.m_surface.text,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("selectionBackground"),
                       scheme.m_surface.selectionBackground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("selectionForeground"),
                       scheme.m_surface.selectionForeground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("caret"),
                       scheme.m_surface.caret,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("caretLine"),
                       scheme.m_surface.caretLine,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("marginBaseBackground"),
                       scheme.m_surface.marginBaseBackground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("marginBaseForeground"),
                       scheme.m_surface.marginBaseForeground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("lineNumberBackground"),
                       scheme.m_surface.lineNumberBackground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("lineNumberForeground"),
                       scheme.m_surface.lineNumberForeground,
                       errors,
                       QStringLiteral("surface"));
        readColorField(surfaceObject,
                       QStringLiteral("foldMarkerForeground"),
                       scheme.m_surface.foldMarkerForeground,
                       errors,
                       QStringLiteral("surface"));
    }

    if (const QJsonValue fallbackValue = root.value(QStringLiteral("languageFallbacks"));
        fallbackValue.isObject()) {
        const QJsonObject fallbackObject = fallbackValue.toObject();
        for (auto it = fallbackObject.begin(); it != fallbackObject.end(); ++it) {
            const QString fromId = normalizeLanguageId(it.key());
            const QString toId = normalizeLanguageId(it.value().toString());
            if (fromId.isEmpty() || toId.isEmpty())
                continue;
            scheme.m_languageFallbacks.insert(fromId, toId);
        }
    }

    if (const QJsonValue languagesValue = root.value(QStringLiteral("languages")); languagesValue.isObject()) {
        const QJsonObject languagesObject = languagesValue.toObject();
        for (auto it = languagesObject.begin(); it != languagesObject.end(); ++it) {
            if (!it.value().isObject())
                continue;
            readLanguagePalette(it.key(), it.value().toObject(), scheme, errors);
        }
    }

    if (!errors.isEmpty()) {
        for (const QString& error : std::as_const(errors)) {
            qCWarning(ceditorlog).noquote()
                << QStringLiteral("CodeEditorStyleManager[%1]: %2").arg(sourceLabel, error);
        }
    }

    return scheme;
}

const CodeEditorStyleManager::SurfaceColors& CodeEditorStyleManager::surfaceColors() const noexcept
{
    return m_surface;
}

bool CodeEditorStyleManager::hasLanguagePalette(QStringView languageId) const
{
    return resolvePalette(languageId) != nullptr;
}

int CodeEditorStyleManager::resolvedStyleCount(QStringView languageId) const
{
    const LanguagePalette* palette = resolvePalette(languageId);
    return palette ? palette->styles.size() : 0;
}

void CodeEditorStyleManager::applyEditorView(QPlainTextEdit* editor) const
{
    if (!editor)
        return;

    editor->setFrameShape(QFrame::NoFrame);
    editor->setStyleSheet(QString());

    QPalette palette = editor->palette();
    palette.setColor(QPalette::Base, m_surface.paper);
    palette.setColor(QPalette::Text, m_surface.text);
    palette.setColor(QPalette::Highlight, m_surface.selectionBackground);
    palette.setColor(QPalette::HighlightedText, m_surface.selectionForeground);
    editor->setPalette(palette);
}

#if CODEEDITOR_HAVE_QSCI
void CodeEditorStyleManager::applyEditorView(QsciScintilla* editor) const
{
    if (!editor)
        return;

    editor->setPaper(m_surface.paper);
    editor->setColor(m_surface.text);
    editor->setEdgeColor(m_surface.paper);
    editor->setCaretForegroundColor(m_surface.caret);
    editor->setCaretLineBackgroundColor(m_surface.caretLine);
    editor->setSelectionBackgroundColor(m_surface.selectionBackground);
    editor->setSelectionForegroundColor(m_surface.selectionForeground);

    editor->setMarginsBackgroundColor(m_surface.marginBaseBackground);
    editor->setMarginsForegroundColor(m_surface.lineNumberForeground);

    const int marginCount = std::max(editor->margins(), 1);
    for (int margin = 0; margin < marginCount; ++margin)
        editor->setMarginBackgroundColor(margin, m_surface.marginBaseBackground);

    editor->setMarginBackgroundColor(0, m_surface.lineNumberBackground);

    editor->setFoldMarginColors(m_surface.foldMarkerForeground, m_surface.marginBaseBackground);
    editor->setIndentationGuidesBackgroundColor(m_surface.paper);
    editor->setIndentationGuidesForegroundColor(m_surface.marginBaseForeground);
}

void CodeEditorStyleManager::applyLexer(QsciLexer* lexer, QStringView languageId) const
{
    if (!lexer)
        return;

    lexer->setDefaultColor(m_surface.text);
    lexer->setDefaultPaper(m_surface.paper);

    const LanguagePalette* palette = resolvePalette(languageId);
    if (!palette)
        return;

    for (const StyleRule& styleRule : palette->styles) {
        if (styleRule.styleId < 0)
            continue;

        if (styleRule.hasForeground)
            lexer->setColor(styleRule.foreground, styleRule.styleId);
        if (styleRule.hasBackground)
            lexer->setPaper(styleRule.background, styleRule.styleId);
    }
}
#endif

CodeEditorStyleManager CodeEditorStyleManager::buildFallback()
{
    CodeEditorStyleManager scheme;

    scheme.m_surface.paper = QColor(QStringLiteral("#15181A"));
    scheme.m_surface.text = QColor(QStringLiteral("#D7E0EA"));
    scheme.m_surface.selectionBackground = QColor(QStringLiteral("#28466F"));
    scheme.m_surface.selectionForeground = QColor(QStringLiteral("#F4F8FC"));
    scheme.m_surface.caret = QColor(QStringLiteral("#E6EDF3"));
    scheme.m_surface.caretLine = QColor(QStringLiteral("#141D2A"));
    scheme.m_surface.marginBaseBackground = scheme.m_surface.paper;
    scheme.m_surface.marginBaseForeground = QColor(QStringLiteral("#6E8092"));
    scheme.m_surface.lineNumberBackground = scheme.m_surface.paper;
    scheme.m_surface.lineNumberForeground = QColor(QStringLiteral("#91A2B7"));
    scheme.m_surface.foldMarkerForeground = QColor(QStringLiteral("#91A2B7"));

    scheme.m_languageFallbacks.insert(QStringLiteral("c"), QStringLiteral("cpp"));
    scheme.m_languageFallbacks.insert(QStringLiteral("json"), QStringLiteral("cpp"));
    scheme.m_languageFallbacks.insert(QStringLiteral("text"), QStringLiteral("cpp"));

    LanguagePalette cppPalette;
    cppPalette.styles = {
        makeRule(1, QStringLiteral("#5F6B7A")),
        makeRule(2, QStringLiteral("#5F6B7A")),
        makeRule(3, QStringLiteral("#657487")),
        makeRule(4, QStringLiteral("#E0AF68")),
        makeRule(5, QStringLiteral("#7AA2F7")),
        makeRule(6, QStringLiteral("#9ECE6A")),
        makeRule(7, QStringLiteral("#9ECE6A")),
        makeRule(10, QStringLiteral("#89DDFF")),
        makeRule(11, QStringLiteral("#D7E0EA")),
        makeRule(12, QStringLiteral("#F7768E")),
        makeRule(16, QStringLiteral("#BB9AF7")),
    };
    scheme.m_languagePalettes.insert(QStringLiteral("cpp"), cppPalette);

    LanguagePalette pythonPalette;
    pythonPalette.styles = {
        makeRule(1, QStringLiteral("#5F6B7A")),
        makeRule(2, QStringLiteral("#E0AF68")),
        makeRule(3, QStringLiteral("#9ECE6A")),
        makeRule(4, QStringLiteral("#9ECE6A")),
        makeRule(5, QStringLiteral("#7AA2F7")),
        makeRule(6, QStringLiteral("#9ECE6A")),
        makeRule(7, QStringLiteral("#9ECE6A")),
        makeRule(8, QStringLiteral("#7DCFFF")),
        makeRule(9, QStringLiteral("#7DCFFF")),
        makeRule(10, QStringLiteral("#89DDFF")),
        makeRule(11, QStringLiteral("#D7E0EA")),
        makeRule(12, QStringLiteral("#657487")),
        makeRule(15, QStringLiteral("#BB9AF7")),
    };
    scheme.m_languagePalettes.insert(QStringLiteral("python"), pythonPalette);

    LanguagePalette xmlPalette;
    xmlPalette.styles = {
        makeRule(1, QStringLiteral("#7AA2F7")),
        makeRule(2, QStringLiteral("#F7768E")),
        makeRule(3, QStringLiteral("#89DDFF")),
        makeRule(4, QStringLiteral("#F7768E")),
        makeRule(5, QStringLiteral("#E0AF68")),
        makeRule(6, QStringLiteral("#9ECE6A")),
        makeRule(7, QStringLiteral("#9ECE6A")),
        makeRule(8, QStringLiteral("#D7E0EA")),
        makeRule(9, QStringLiteral("#5F6B7A")),
        makeRule(10, QStringLiteral("#BB9AF7")),
        makeRule(11, QStringLiteral("#7AA2F7")),
        makeRule(12, QStringLiteral("#7AA2F7")),
        makeRule(13, QStringLiteral("#7AA2F7")),
    };
    scheme.m_languagePalettes.insert(QStringLiteral("xml"), xmlPalette);

    return scheme;
}

QString CodeEditorStyleManager::normalizeLanguageId(QStringView languageId)
{
    return languageId.toString().trimmed().toLower();
}

QColor CodeEditorStyleManager::parseColor(const QString& value)
{
    QColor color(value.trimmed());
    return color;
}

bool CodeEditorStyleManager::readColorField(const QJsonObject& object,
                                            const QString& key,
                                            QColor& inOutColor,
                                            QStringList& errors,
                                            const QString& context)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
        return false;

    if (!value.isString()) {
        errors.push_back(
            QStringLiteral("%1.%2 must be a string color value.").arg(context, key));
        return false;
    }

    const QColor parsed = parseColor(value.toString());
    if (!parsed.isValid()) {
        errors.push_back(
            QStringLiteral("%1.%2 has invalid color value '%3'.").arg(context, key, value.toString()));
        return false;
    }

    inOutColor = parsed;
    return true;
}

void CodeEditorStyleManager::readLanguagePalette(const QString& languageId,
                                                 const QJsonObject& languageObject,
                                                 CodeEditorStyleManager& inOutScheme,
                                                 QStringList& errors)
{
    const QString normalizedId = normalizeLanguageId(languageId);
    if (normalizedId.isEmpty())
        return;

    const QJsonValue stylesValue = languageObject.value(QStringLiteral("styles"));
    if (!stylesValue.isArray()) {
        errors.push_back(QStringLiteral("languages.%1.styles must be an array.").arg(languageId));
        return;
    }

    LanguagePalette palette;
    const QJsonArray stylesArray = stylesValue.toArray();
    palette.styles.reserve(stylesArray.size());

    for (int index = 0; index < stylesArray.size(); ++index) {
        const QJsonValue ruleValue = stylesArray.at(index);
        if (!ruleValue.isObject()) {
            errors.push_back(QStringLiteral("languages.%1.styles[%2] must be an object.")
                                 .arg(languageId)
                                 .arg(index));
            continue;
        }

        const QJsonObject ruleObject = ruleValue.toObject();
        const QJsonValue idValue = ruleObject.value(QStringLiteral("id"));
        if (!idValue.isDouble()) {
            errors.push_back(QStringLiteral("languages.%1.styles[%2].id must be a number.")
                                 .arg(languageId)
                                 .arg(index));
            continue;
        }

        const int styleId = idValue.toInt(-1);
        if (styleId < 0) {
            errors.push_back(QStringLiteral("languages.%1.styles[%2].id must be >= 0.")
                                 .arg(languageId)
                                 .arg(index));
            continue;
        }

        StyleRule rule;
        rule.styleId = styleId;

        if (const QJsonValue foregroundValue = ruleObject.value(QStringLiteral("foreground"));
            foregroundValue.isString()) {
            const QColor color = parseColor(foregroundValue.toString());
            if (color.isValid()) {
                rule.foreground = color;
                rule.hasForeground = true;
            } else {
                errors.push_back(QStringLiteral("languages.%1.styles[%2].foreground has invalid color '%3'.")
                                     .arg(languageId)
                                     .arg(index)
                                     .arg(foregroundValue.toString()));
            }
        }

        if (const QJsonValue backgroundValue = ruleObject.value(QStringLiteral("background"));
            backgroundValue.isString()) {
            const QColor color = parseColor(backgroundValue.toString());
            if (color.isValid()) {
                rule.background = color;
                rule.hasBackground = true;
            } else {
                errors.push_back(QStringLiteral("languages.%1.styles[%2].background has invalid color '%3'.")
                                     .arg(languageId)
                                     .arg(index)
                                     .arg(backgroundValue.toString()));
            }
        }

        if (!rule.hasForeground && !rule.hasBackground)
            continue;

        palette.styles.push_back(rule);
    }

    if (!palette.styles.isEmpty())
        inOutScheme.m_languagePalettes.insert(normalizedId, palette);
}

const CodeEditorStyleManager::LanguagePalette* CodeEditorStyleManager::resolvePalette(QStringView languageId) const
{
    QString currentId = normalizeLanguageId(languageId);
    if (currentId.isEmpty())
        return nullptr;

    QSet<QString> visited;
    while (!currentId.isEmpty()) {
        const auto it = m_languagePalettes.constFind(currentId);
        if (it != m_languagePalettes.cend())
            return &it.value();

        if (visited.contains(currentId))
            break;
        visited.insert(currentId);
        currentId = m_languageFallbacks.value(currentId);
    }

    return nullptr;
}

} // namespace CodeEditor::Style
