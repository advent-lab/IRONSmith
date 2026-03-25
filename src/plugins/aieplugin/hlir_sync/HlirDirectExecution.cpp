// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/hlir_sync/HlirDirectExecution.hpp"
#include "aieplugin/hlir_sync/HlirSyncService.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QThread>

namespace Aie::Internal {

HlirDirectExecution::HlirDirectExecution(QObject* parent)
    : QObject(parent)
{
}

void HlirDirectExecution::execute(const QString& outputDir)
{
    if (outputDir.isEmpty()) {
        emit executeFinished(false, tr("No design is open."));
        return;
    }

    const QString scriptPath = outputDir + QStringLiteral("/generated_design.py");
    if (!QFileInfo::exists(scriptPath)) {
        emit executeFinished(false,
            tr("No generated code found — run Generate Code first.\n\nExpected: %1")
            .arg(scriptPath));
        return;
    }

    emit runStarted();
    QCoreApplication::processEvents();

    emit stepLogged(true, tr("Executing generated_design.py"));
    QCoreApplication::processEvents();

    QProcess process;
    process.setWorkingDirectory(outputDir);
    process.setProcessChannelMode(QProcess::MergedChannels);

    const QString pythonExe = QStringLiteral(PYTHON_EXECUTABLE);
    process.start(pythonExe, {scriptPath});

    if (!process.waitForStarted(5000)) {
        emit stepLogged(false, tr("Failed to start Python interpreter"));
        emit executeFinished(false, tr("Could not launch: %1").arg(pythonExe));
        return;
    }

    // Pump the event loop while the script runs so the UI stays responsive.
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        QCoreApplication::processEvents();
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const bool success = (process.exitStatus() == QProcess::NormalExit)
                      && (process.exitCode() == 0);

    emit stepLogged(success,
        success ? tr("Script exited successfully")
                : tr("Script exited with code %1").arg(process.exitCode()));
    QCoreApplication::processEvents();
    if (HlirSyncService::animateSteps()) QThread::msleep(250);

    const QString header = success ? tr("Execution succeeded.") : tr("Execution failed.");
    emit executeFinished(success, output.isEmpty() ? header : header + u'\n' + output);
}

} // namespace Aie::Internal
