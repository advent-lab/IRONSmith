// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QStringList>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Aie::Internal {

class StmtListWidget; // defined in BodyStmtsEditor.cpp

/// Visual editor for a core function's body_stmts JSON.
///
/// Displays three param chip rows (Kernel / Inputs / Outputs) and a recursive
/// list of statement rows (ACQUIRE / RELEASE / FOR LOOP / KERNEL CALL / ASSIGN).
/// Every edit emits jsonChanged() with the serialised object-format JSON:
///   { "params": [...], "body": [...] }
/// The params order is always: kernels first, then inputs, then outputs.
///
/// setJson() accepts both that object format and the legacy array format.
class BodyStmtsEditor : public QWidget
{
    Q_OBJECT

public:
    explicit BodyStmtsEditor(QWidget* parent = nullptr);

    /// Load state from a body_stmts JSON string (array or {"params","body"} format).
    void setJson(const QString& json);

    /// Serialise current state to compact object-format JSON string.
    QString toJson() const;

    /// True when all param buckets are empty and no statement rows exist.
    bool isEmpty() const;

    // Called by internal chip remove buttons (must be public for free-function helper).
    void removeParam(const QString& name, int bucket); // bucket: 0=kernel,1=input,2=output

signals:
    void jsonChanged(const QString& json);

private:
    enum Bucket { Kernel = 0, Input = 1, Output = 2 };

    void rebuildParamChips();
    void addParam(const QString& name, Bucket bucket);
    QStringList allParams() const;
    void onChanged();

    // bucket 0 = kernels, 1 = inputs, 2 = outputs
    QStringList  m_params[3];

    QWidget*     m_chipsRow[3]    = {};
    QHBoxLayout* m_chipsLayout[3] = {};

    StmtListWidget* m_stmtList = nullptr;
};

} // namespace Aie::Internal
