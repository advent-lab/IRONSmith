// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <QtCore/QList>
#include <QtCore/QString>

#include <memory>

namespace Canvas {
class CanvasDocument;
} // namespace Canvas

namespace Aie::Internal {

/// A single issue produced by a design rule check.
struct VerificationIssue {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    QString message;
};

/// Data passed to each verification check.
struct VerificationContext {
    const Canvas::CanvasDocument* document = nullptr;
};

/// Abstract interface for a single design rule check.
class IVerificationCheck {
public:
    virtual ~IVerificationCheck() = default;

    /// Internal identifier used in diagnostics.
    virtual QString name() const = 0;

    /// Short user-visible label shown in the log while verification runs.
    virtual QString displayName() const = 0;

    /// Run the check and return all issues found (empty = passed).
    virtual QList<VerificationIssue> run(const VerificationContext& ctx) const = 0;
};

/// Snapshot of design metrics, collected independently of verification checks.
struct DesignStats {
    int shimTiles = 0;
    int memTiles  = 0;
    int aieTiles  = 0;
    int fifos     = 0;
    int fills     = 0;
    int drains    = 0;
    int splits      = 0;
    int joins       = 0;
    int broadcasts  = 0;
};

/// Collect tile and FIFO counts from the canvas without running checks.
AIEPLUGIN_EXPORT DesignStats collectStats(const VerificationContext& ctx);

/// Per-check result returned by verifyDetailed().
struct CheckResult {
    QString displayName;
    QList<VerificationIssue> issues;
};

/// Runs all registered checks and aggregates their results.
class AIEPLUGIN_EXPORT DesignVerifier {
public:
    DesignVerifier();
    ~DesignVerifier();

    /// Run every registered check and return all issues found.
    QList<VerificationIssue> verify(const VerificationContext& ctx) const;

    /// Run every check individually and return per-check results (for progress logging).
    QList<CheckResult> verifyDetailed(const VerificationContext& ctx) const;

    /// Returns true if the list contains at least one Error-severity issue.
    static bool hasErrors(const QList<VerificationIssue>& issues);

private:
    std::vector<std::unique_ptr<IVerificationCheck>> m_checks;
};

} // namespace Aie::Internal
