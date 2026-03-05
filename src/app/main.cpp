// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLibrary>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>

#include "extensionsystem/PluginManager.hpp"
#include "extensionsystem/PluginSpec.hpp"

#ifdef IRONSMITH_WITH_BRIDGE_TESTS
#include "BridgeTests.hpp"
#endif

using namespace ExtensionSystem;

static constexpr char corePluginIdC[] = "Core";

#ifdef Q_OS_WIN
static void addLibrarySearchPaths()
{
	const QDir appDir(QCoreApplication::applicationDirPath());

	// Prepend lib/ironsmith/ and lib/ironsmith/plugins/ to PATH so the
	// Windows loader can resolve DLL dependencies when plugins are loaded.
	const QString libDir = QDir::cleanPath(appDir.absoluteFilePath("../lib/ironsmith"));
	const QString pluginDir = QDir::cleanPath(appDir.absoluteFilePath("../lib/ironsmith/plugins"));

	QByteArray path = qgetenv("PATH");
	if (QDir(pluginDir).exists())
		path.prepend(pluginDir.toLocal8Bit() + ";");
	if (QDir(libDir).exists())
		path.prepend(libDir.toLocal8Bit() + ";");
	qputenv("PATH", path);
}
#endif

static QString defaultPluginDir()
{
	QDir d(QCoreApplication::applicationDirPath());
	if (!d.cdUp())
		return {};
	if (!d.cd("lib"))
		return {};
	if (!d.cd("ironsmith"))
		return {};
	if (!d.cd("plugins"))
		return {};
	return d.absolutePath();
}

static bool registerSystemPluginsFromDir(const QString& pluginDir)
{
	QDir d(pluginDir);
	if (!d.exists())
		return false;

	QStringList files;
	const QFileInfoList infos = d.entryInfoList(QDir::Files);
	for (const QFileInfo& fi : infos) {
		const QString abs = fi.absoluteFilePath();
		if (QLibrary::isLibrary(abs))
			files.push_back(abs);
	}

	return PluginManager::registerPlugins(files);
}

static void printErrorsAndFail(const QString& header, const QStringList& errors)
{
	qCritical().noquote() << header;
	for (const QString& e : errors)
		qCritical().noquote() << "  " << e;
}

int main(int argc, char** argv)
{
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);

	QApplication app(argc, argv);

#ifdef Q_OS_WIN
	addLibrarySearchPaths();
#endif

#ifdef IRONSMITH_WITH_BRIDGE_TESTS
	if (app.arguments().contains("--test-bridges"))
		return BridgeTests::runBridgeTests() ? EXIT_SUCCESS : EXIT_FAILURE;
#endif

	const QString pluginDir = defaultPluginDir();
	if (pluginDir.isEmpty() || !registerSystemPluginsFromDir(pluginDir)) {
		printErrorsAndFail("Failed to register system plugins.", PluginManager::lastErrors());
		return EXIT_FAILURE;
	}

	PluginSpec* core = PluginManager::specById(corePluginIdC);
	if (!core) {
		printErrorsAndFail("Core plugin spec not found.", PluginManager::lastErrors());
		return EXIT_FAILURE;
	}
	if (!core->isEffectivelyEnabled()) {
		printErrorsAndFail("Core plugin is not enabled.", core->errors());
		return EXIT_FAILURE;
	}
	if (core->hasError()) {
		printErrorsAndFail("Core plugin spec has errors.", core->errors());
		return EXIT_FAILURE;
	}

	if (!PluginManager::loadPlugins(app.arguments())) {
		printErrorsAndFail("Failed to load plugins.", PluginManager::lastErrors());
		return EXIT_FAILURE;
	}

	if (core->hasError()) {
		printErrorsAndFail("Core plugin failed during load.", core->errors());
		return EXIT_FAILURE;
	}

	return app.exec();
}
