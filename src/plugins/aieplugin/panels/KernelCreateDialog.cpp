// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/KernelCreateDialog.hpp"

#include <utils/PathUtils.hpp>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QVBoxLayout>

namespace Aie::Internal {

namespace {

QString defaultNameForId(const QString& kernelId)
{
    QString name = kernelId.trimmed();
    name.replace('_', ' ');
    name.replace('-', ' ');
    return name;
}

QString defaultSignatureForId(const QString& kernelId)
{
    QString functionName = Utils::PathUtils::sanitizeFileName(kernelId);
    functionName.replace(' ', '_');
    functionName.replace('.', '_');
    functionName.replace('-', '_');
    if (functionName.isEmpty())
        functionName = QStringLiteral("kernel_entry");
    return QStringLiteral("void %1();").arg(functionName);
}

QString elidedPathText(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QStringLiteral("(not available)");
    return path;
}

} // namespace

KernelCreateDialog::KernelCreateDialog(bool workspaceAvailable,
                                       const QString& workspaceRoot,
                                       const QString& globalRoot,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Create Kernel Toolbox Entry"));
    setModal(true);
    resize(560, 320);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* introLabel = new QLabel(
        QStringLiteral("Create a user-defined kernel scaffold and register it in the Kernels catalog."),
        this);
    introLabel->setWordWrap(true);
    rootLayout->addWidget(introLabel);

    auto* formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->setFormAlignment(Qt::AlignTop);

    m_idField = new QLineEdit(this);
    m_idField->setPlaceholderText(QStringLiteral("gemm_bf16_custom"));

    m_nameField = new QLineEdit(this);
    m_nameField->setPlaceholderText(QStringLiteral("GEMM (BF16) Custom"));

    m_signatureField = new QLineEdit(this);
    m_signatureField->setPlaceholderText(QStringLiteral("void gemm_bf16_custom();"));

    m_descriptionField = new QLineEdit(this);
    m_descriptionField->setPlaceholderText(QStringLiteral("Custom kernel scaffold."));

    formLayout->addRow(QStringLiteral("Kernel ID"), m_idField);
    formLayout->addRow(QStringLiteral("Display Name"), m_nameField);
    formLayout->addRow(QStringLiteral("Signature"), m_signatureField);
    formLayout->addRow(QStringLiteral("Description"), m_descriptionField);

    rootLayout->addLayout(formLayout);

    auto* scopeGroup = new QGroupBox(QStringLiteral("Storage Scope"), this);
    auto* scopeLayout = new QVBoxLayout(scopeGroup);
    scopeLayout->setContentsMargins(8, 8, 8, 8);
    scopeLayout->setSpacing(6);

    m_workspaceScopeButton = new QRadioButton(QStringLiteral("Workspace (current project)"), scopeGroup);
    m_globalScopeButton = new QRadioButton(QStringLiteral("Application Global (all workspaces)"), scopeGroup);

    auto* workspacePathLabel = new QLabel(QStringLiteral("Path: %1").arg(elidedPathText(workspaceRoot)), scopeGroup);
    workspacePathLabel->setWordWrap(true);
    workspacePathLabel->setStyleSheet(QStringLiteral("QLabel { color: #9da4aa; font-size: 11px; }"));

    auto* globalPathLabel = new QLabel(QStringLiteral("Path: %1").arg(elidedPathText(globalRoot)), scopeGroup);
    globalPathLabel->setWordWrap(true);
    globalPathLabel->setStyleSheet(QStringLiteral("QLabel { color: #9da4aa; font-size: 11px; }"));

    scopeLayout->addWidget(m_workspaceScopeButton);
    scopeLayout->addWidget(workspacePathLabel);
    scopeLayout->addWidget(m_globalScopeButton);
    scopeLayout->addWidget(globalPathLabel);

    m_workspaceScopeButton->setEnabled(workspaceAvailable);
    if (workspaceAvailable) {
        m_workspaceScopeButton->setChecked(true);
    } else {
        m_globalScopeButton->setChecked(true);
    }

    rootLayout->addWidget(scopeGroup);

    m_openInEditorCheck = new QCheckBox(QStringLiteral("Open kernel source in code editor after creation"), this);
    m_openInEditorCheck->setChecked(true);
    rootLayout->addWidget(m_openInEditorCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &KernelCreateDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    connect(m_idField, &QLineEdit::textChanged, this, [this]() { syncSuggestionsFromId(); });
    syncSuggestionsFromId();
}

KernelCreateDialogResult KernelCreateDialog::result() const
{
    KernelCreateDialogResult out;
    out.id = m_idField ? m_idField->text().trimmed() : QString();
    out.name = m_nameField ? m_nameField->text().trimmed() : QString();
    out.signature = m_signatureField ? m_signatureField->text().trimmed() : QString();
    out.description = m_descriptionField ? m_descriptionField->text().trimmed() : QString();
    out.scope = (m_globalScopeButton && m_globalScopeButton->isChecked())
                    ? KernelSourceScope::Global
                    : KernelSourceScope::Workspace;
    out.openInEditor = m_openInEditorCheck && m_openInEditorCheck->isChecked();
    return out;
}

void KernelCreateDialog::syncSuggestionsFromId()
{
    if (!m_idField || !m_nameField || !m_signatureField)
        return;

    const QString kernelId = m_idField->text().trimmed();
    const QString suggestedName = defaultNameForId(kernelId);
    const QString suggestedSignature = defaultSignatureForId(kernelId);

    const QString currentName = m_nameField->text().trimmed();
    if (currentName.isEmpty() || currentName == m_lastSuggestedName)
        m_nameField->setText(suggestedName);

    const QString currentSignature = m_signatureField->text().trimmed();
    if (currentSignature.isEmpty() || currentSignature == m_lastSuggestedSignature)
        m_signatureField->setText(suggestedSignature);

    m_lastSuggestedName = suggestedName;
    m_lastSuggestedSignature = suggestedSignature;
}

void KernelCreateDialog::accept()
{
    if (!m_idField || m_idField->text().trimmed().isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Create Kernel"),
                             QStringLiteral("Kernel ID is required."));
        return;
    }

    QDialog::accept();
}

} // namespace Aie::Internal

