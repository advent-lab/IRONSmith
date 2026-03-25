// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "utils/Environment.hpp"
#include "utils/UtilsGlobal.hpp"

#include <QtCore/QSettings>

#include <memory>

namespace Utils {

class UTILS_EXPORT QtEnvironmentPersistencePolicy final {
public:
	struct SettingsHandle final {
		std::unique_ptr<QSettings> settings;
	};

	EnvironmentPaths resolvePaths(const EnvironmentConfig& cfg) const;

	SettingsHandle openSettings(EnvironmentScope scope, const EnvironmentPaths& paths) const;
	QVariant settingsValue(const SettingsHandle& h, QStringView key, const QVariant& def) const;
	void setSettingsValue(SettingsHandle& h, QStringView key, const QVariant& value) const;
	void removeSettingsKey(SettingsHandle& h, QStringView key) const;
	bool settingsContains(const SettingsHandle& h, QStringView key) const;
	void syncSettings(SettingsHandle& h) const;

	bool ensureScopeStorage(EnvironmentScope scope, const EnvironmentPaths& paths, QString* error) const;

	bool readStateBytes(EnvironmentScope scope, const EnvironmentPaths& paths, QStringView name,
						bool useBackup, QByteArray* out, QString* error) const;

	bool writeStateBytesAtomic(EnvironmentScope scope, const EnvironmentPaths& paths, QStringView name,
							   const QByteArray& bytes, QString* error) const;

	bool removeState(EnvironmentScope scope, const EnvironmentPaths& paths, QStringView name,
					 bool removeBackup, QString* error) const;

private:
	QString settingsFilePath(EnvironmentScope scope, const EnvironmentPaths& paths) const;
	QString stateFilePath(EnvironmentScope scope, const EnvironmentPaths& paths, QStringView name, bool backup) const;
};

using Environment = BasicEnvironment<QtEnvironmentPersistencePolicy>;

} // namespace Utils
