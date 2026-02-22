// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <QtCore/QPointer>
#include <QtCore/QString>

#include <QtWidgets/QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLineEdit;
class QRadioButton;
QT_END_NAMESPACE

namespace Aie::Internal {

struct KernelCreateDialogResult final {
    QString id;
    QString name;
    QString signature;
    QString description;
    KernelSourceScope scope = KernelSourceScope::Workspace;
    bool openInEditor = true;
};

class KernelCreateDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit KernelCreateDialog(bool workspaceAvailable,
                                const QString& workspaceRoot,
                                const QString& globalRoot,
                                QWidget* parent = nullptr);

    KernelCreateDialogResult result() const;

private:
    void syncSuggestionsFromId();
    void accept() override;

    QPointer<QLineEdit> m_idField;
    QPointer<QLineEdit> m_nameField;
    QPointer<QLineEdit> m_signatureField;
    QPointer<QLineEdit> m_descriptionField;
    QPointer<QRadioButton> m_workspaceScopeButton;
    QPointer<QRadioButton> m_globalScopeButton;
    QPointer<QCheckBox> m_openInEditorCheck;

    QString m_lastSuggestedName;
    QString m_lastSuggestedSignature;
};

} // namespace Aie::Internal

