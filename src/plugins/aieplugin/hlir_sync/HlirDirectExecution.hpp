// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

namespace Aie::Internal {

/// Executes the generated design script (codegen/generated_design.py) as an
/// external Python process and streams the output back to the log panel.
class HlirDirectExecution : public QObject
{
    Q_OBJECT

public:
    explicit HlirDirectExecution(QObject* parent = nullptr);

public slots:
    /// Run generated_design.py in <outputDir>/codegen/.
    /// Emits runStarted(), stepLogged(), and executeFinished() as the run progresses.
    void execute(const QString& outputDir);

signals:
    void runStarted();
    void stepLogged(bool ok, const QString& label);
    void executeFinished(bool success, const QString& message);
};

} // namespace Aie::Internal
