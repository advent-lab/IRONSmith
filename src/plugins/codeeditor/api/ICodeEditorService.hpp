// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/CodeEditorGlobal.hpp"
#include "codeeditor/api/CodeEditorTypes.hpp"

#include <utils/Result.hpp>

#include <QtCore/QObject>
#include <QtCore/QVector>
#include <qnamespace.h>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace CodeEditor::Api {

class CODEEDITOR_EXPORT ICodeEditorService : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;
	~ICodeEditorService() override = default;

	virtual Utils::Result openFile(const CodeEditorOpenRequest& request, CodeEditorSessionHandle& outHandle) = 0;
	virtual Utils::Result closeFile(const CodeEditorSessionHandle& handle, CodeEditorCloseReason reason) = 0;
	virtual Utils::Result closeAllFiles(CodeEditorCloseReason reason) = 0;

	virtual Utils::Result saveFile(const CodeEditorSessionHandle& handle) = 0;
	virtual Utils::Result saveAllFiles() = 0;

	virtual Utils::Result setActiveFile(const CodeEditorSessionHandle& handle) = 0;
	virtual Utils::Result updateFilePath(const CodeEditorSessionHandle& handle, const QString& newFilePath) = 0;

	virtual CodeEditorSessionHandle activeFile() const = 0;
	virtual bool hasOpenFile() const = 0;
	virtual QVector<CodeEditorSessionHandle> openFiles() const = 0;
	virtual bool isDirty(const CodeEditorSessionHandle& handle) const = 0;

	virtual QWidget* widgetForSession(const CodeEditorSessionHandle& handle) const = 0;
	virtual QWidget* createQuickView(const CodeEditorQuickViewRequest& request, QWidget* parent = nullptr) = 0;

	virtual bool supportsLanguage(const QString& languageId) const = 0;

	virtual int zoomLevel() const = 0;
	virtual void setZoomLevel(int level) = 0;

signals:
	void fileOpened(const CodeEditor::Api::CodeEditorSessionHandle& handle);
	void fileClosed(const CodeEditor::Api::CodeEditorSessionHandle& handle,
					CodeEditor::Api::CodeEditorCloseReason reason);
	void activeFileChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle);
	void fileDirtyStateChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle, bool dirty);
	void filePathChanged(const CodeEditor::Api::CodeEditorSessionHandle& handle,
						 const QString& oldFilePath,
						 const QString& newFilePath);
	void zoomLevelChanged(int level);
};
} // namespace CodeEditor::Api
