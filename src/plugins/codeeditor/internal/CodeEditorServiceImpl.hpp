// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/api/ICodeEditorService.hpp"
#include "codeeditor/CodeEditorTextView.hpp"

#include <utils/Result.hpp>

#include <QtCore/QFileSystemWatcher>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QVector>

namespace Core {
class IUiHost;
}

namespace CodeEditor::Internal {

class CodeEditorServiceImpl final : public CodeEditor::Api::ICodeEditorService {
	Q_OBJECT

public:
	explicit CodeEditorServiceImpl(QObject* parent = nullptr);

	void setUiHost(Core::IUiHost* uiHost);

	Utils::Result openFile(const CodeEditor::Api::CodeEditorOpenRequest &request,
							CodeEditor::Api::CodeEditorSessionHandle &outHandle) override;
	Utils::Result closeFile(const CodeEditor::Api::CodeEditorSessionHandle &handle,
							CodeEditor::Api::CodeEditorCloseReason reason) override;
	Utils::Result closeAllFiles(CodeEditor::Api::CodeEditorCloseReason reason) override;

	Utils::Result saveFile(const CodeEditor::Api::CodeEditorSessionHandle& handle) override;
	Utils::Result saveAllFiles() override;

	Utils::Result setActiveFile(const CodeEditor::Api::CodeEditorSessionHandle& handle) override;
	Utils::Result updateFilePath(const CodeEditor::Api::CodeEditorSessionHandle& handle,
								 const QString& newFilePath) override;

	CodeEditor::Api::CodeEditorSessionHandle activeFile() const override;
	bool hasOpenFile() const override;
	QVector<CodeEditor::Api::CodeEditorSessionHandle> openFiles() const override;
	bool isDirty(const CodeEditor::Api::CodeEditorSessionHandle& handle) const override;

	QWidget* widgetForSession(const Api::CodeEditorSessionHandle &handle) const override;
	QWidget* createQuickView(const CodeEditor::Api::CodeEditorQuickViewRequest& request,
							 QWidget* parent = nullptr) override;

	bool supportsLanguage(const QString &languageId) const override;
	int zoomLevel() const override;
	void setZoomLevel(int level) override;

private:
	struct SessionState final {
		Api::CodeEditorSessionHandle handle;
		QPointer<CodeEditorTextView> view;
		QString persistedText;
		QString currentText;
		bool forcedReadOnly = false;
		bool dirty = false;
	};

	Utils::Result setActiveFileById(const QString& id);
    void revealCodeSidebar();
	static QString normalizeAbsolutePath(const QString& path);

	void updateDirtyState(SessionState& state, bool dirty);
	void connectSessionView(const QString& sessionId, CodeEditorTextView* view);
	CodeEditorTextView* ensureView(const QString& sessionId);
	SessionState* mutableSessionById(const QString& id);
	const SessionState* sessionById(const QString& id) const;

	void watchPath(const QString& absolutePath);
	void unwatchPathIfUnused(const QString& absolutePath);
	void handleWatchedFileChanged(const QString& path);

	static QString resolveLanguageId(const QString& hint, const QString& absolutePath);
	static Utils::Result readTextFile(const QString& absolutePath, QString& outText, quint64 maxBytes);
	static Utils::Result writeTextFile(const QString& absolutePath, const QString& text);

	QPointer<Core::IUiHost> m_uiHost;
	QHash<QString, SessionState> m_sessionsById;
	QHash<QString, QString> m_sessionIdByPath;
	QVector<QString> m_openOrder;
	mutable QFileSystemWatcher m_fileWatcher;
	CodeEditor::Api::CodeEditorSessionHandle m_active;
	int m_globalZoomLevel = 0;
};
} // namespace CodeEditor::Internal
