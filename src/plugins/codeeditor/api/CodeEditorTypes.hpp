// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "codeeditor/CodeEditorGlobal.hpp"

#include <QtCore/QMetaType>
#include <QtCore/QString>

namespace CodeEditor::Api {

struct CODEEDITOR_EXPORT CodeEditorSessionHandle final {
	QString id;
	QString filePath;
	QString languageId;
	bool readOnly = false;

	bool isValid() const noexcept {
		return !id.trimmed().isEmpty() && !filePath.trimmed().isEmpty();
	}

	explicit operator bool () const noexcept { return isValid(); }
};

struct CODEEDITOR_EXPORT CodeEditorOpenRequest final {
	QString filePath;
	QString languageHint;
	bool activate = true;
	bool readOnly = false;
};

struct CODEEDITOR_EXPORT CodeEditorQuickViewRequest final {
	QString filePath;
	QString languageHint;
};

enum class CODEEDITOR_EXPORT CodeEditorCloseReason : unsigned char {
	UserClosed,
	FileDeleted,
	WorkspaceChanged,
	Shutdown,
	Error
};

} // namespace CodeEditor::Api

Q_DECLARE_METATYPE(CodeEditor::Api::CodeEditorSessionHandle)
Q_DECLARE_METATYPE(CodeEditor::Api::CodeEditorCloseReason)