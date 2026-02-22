// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QtGlobal>

#if defined(CODEEDITOR_BUILD_SHARED) && (CODEEDITOR_BUILD_SHARED == 1)
#	if defined(CODEEDITOR_LIBRARY)
#		define CODEEDITOR_EXPORT Q_DECL_EXPORT
#	else
#		define CODEEDITOR_EXPORT Q_DECL_IMPORT
#	endif
#else
#	define CODEEDITOR_EXPORT
#endif

Q_DECLARE_LOGGING_CATEGORY(ceditorlog)