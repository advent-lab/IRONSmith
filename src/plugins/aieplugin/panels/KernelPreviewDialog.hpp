// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <QtCore/QPointer>
#include <QtCore/QPoint>

#include <QtWidgets/QDialog>
#include <QtWidgets/QPushButton>

QT_BEGIN_NAMESPACE
class QEvent;
class QLabel;
class QTextEdit;
class QTabWidget;
class QToolButton;
class QWidget;
QT_END_NAMESPACE

namespace Aie::Internal {

class KernelPreviewDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class Tab : unsigned char {
        Code = 0,
        Metadata = 1
    };

    explicit KernelPreviewDialog(const KernelAsset& kernel, QWidget* parent = nullptr);

    QPushButton* openInEditorButton() const { return m_openInEditorButton; }
    QPushButton* copyToWorkspaceButton() const { return m_copyToWorkspaceButton; }
    QPushButton* copyToGlobalButton() const { return m_copyToGlobalButton; }

    void setCodeText(const QString& codeText);
    void setCodeWidget(QWidget* codeWidget);
    void setMetadataText(const QString& metadataText);
    void setMetadataWidget(QWidget* metadataWidget);
    void setActiveTab(Tab tab);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void toggleMaximizeRestore();

    KernelAsset m_kernel;

    QPointer<QWidget> m_windowChrome;
    QPointer<QLabel> m_windowTitleLabel;
    QPointer<QToolButton> m_closeChromeButton;
    QPointer<QToolButton> m_expandChromeButton;
    QPointer<QToolButton> m_minimizeChromeButton;

    QPointer<QLabel> m_titleLabel;
    QPointer<QLabel> m_scopeLabel;
    QPointer<QLabel> m_signatureLabel;
    QPointer<QTabWidget> m_tabs;
    QPointer<QTextEdit> m_codeView;
    QPointer<QTextEdit> m_metadataView;

    QPointer<QPushButton> m_openInEditorButton;
    QPointer<QPushButton> m_copyToWorkspaceButton;
    QPointer<QPushButton> m_copyToGlobalButton;

    bool m_draggingWindow = false;
    QPoint m_dragOffset;
};

} // namespace Aie::Internal
