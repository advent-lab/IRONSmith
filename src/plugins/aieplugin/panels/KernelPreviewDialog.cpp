// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/KernelPreviewDialog.hpp"

#include "aieplugin/kernels/KernelCatalog.hpp"

#include <QtCore/QEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayoutItem>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

namespace Aie::Internal {

KernelPreviewDialog::KernelPreviewDialog(const KernelAsset& kernel, QWidget* parent)
    : QDialog(parent)
    , m_kernel(kernel)
{
    setObjectName(QStringLiteral("AieKernelPreviewDialog"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::Window);
    setModal(true);
    resize(760, 560);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_windowChrome = new QWidget(this);
    m_windowChrome->setObjectName(QStringLiteral("AieKernelPreviewChrome"));
    m_windowChrome->installEventFilter(this);
    auto* chromeLayout = new QHBoxLayout(m_windowChrome);
    chromeLayout->setContentsMargins(10, 8, 10, 8);
    chromeLayout->setSpacing(8);

    m_closeChromeButton = new QToolButton(m_windowChrome);
    m_closeChromeButton->setObjectName(QStringLiteral("AieKernelPreviewChromeCloseButton"));
    m_closeChromeButton->setText(QStringLiteral("x"));
    m_closeChromeButton->setToolTip(QStringLiteral("Close"));
    m_closeChromeButton->setCursor(Qt::PointingHandCursor);
    connect(m_closeChromeButton, &QToolButton::clicked, this, &QDialog::reject);

    m_minimizeChromeButton = new QToolButton(m_windowChrome);
    m_minimizeChromeButton->setObjectName(QStringLiteral("AieKernelPreviewChromeMinimizeButton"));
    m_minimizeChromeButton->setText(QStringLiteral("-"));
    m_minimizeChromeButton->setToolTip(QStringLiteral("Minimize"));
    m_minimizeChromeButton->setCursor(Qt::PointingHandCursor);
    connect(m_minimizeChromeButton, &QToolButton::clicked, this, [this]() { showMinimized(); });

    m_expandChromeButton = new QToolButton(m_windowChrome);
    m_expandChromeButton->setObjectName(QStringLiteral("AieKernelPreviewChromeExpandButton"));
    m_expandChromeButton->setText(QStringLiteral("+"));
    m_expandChromeButton->setToolTip(QStringLiteral("Expand / Restore"));
    m_expandChromeButton->setCursor(Qt::PointingHandCursor);
    connect(m_expandChromeButton, &QToolButton::clicked, this, &KernelPreviewDialog::toggleMaximizeRestore);

    m_windowTitleLabel = new QLabel(QStringLiteral("Kernel Preview"), m_windowChrome);
    m_windowTitleLabel->setObjectName(QStringLiteral("AieKernelPreviewChromeTitle"));
    m_windowTitleLabel->installEventFilter(this);

    chromeLayout->addWidget(m_closeChromeButton, 0, Qt::AlignVCenter);
    chromeLayout->addWidget(m_minimizeChromeButton, 0, Qt::AlignVCenter);
    chromeLayout->addWidget(m_expandChromeButton, 0, Qt::AlignVCenter);
    chromeLayout->addStretch(1);
    chromeLayout->addWidget(m_windowTitleLabel, 0, Qt::AlignCenter);
    chromeLayout->addStretch(1);

    rootLayout->addWidget(m_windowChrome);

    auto* contentHost = new QWidget(this);
    contentHost->setObjectName(QStringLiteral("AieKernelPreviewContent"));
    auto* contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(10);
    rootLayout->addWidget(contentHost, 1);

    m_titleLabel = new QLabel(kernel.name, contentHost);
    m_titleLabel->setObjectName(QStringLiteral("AieKernelPreviewTitle"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_scopeLabel = new QLabel(QStringLiteral("Scope: %1").arg(kernelScopeName(kernel.scope)), contentHost);
    m_scopeLabel->setObjectName(QStringLiteral("AieKernelPreviewScope"));
    m_signatureLabel = new QLabel(kernel.signature.trimmed(), contentHost);
    m_signatureLabel->setObjectName(QStringLiteral("AieKernelPreviewSignature"));
    m_signatureLabel->setWordWrap(true);

    contentLayout->addWidget(m_titleLabel);
    contentLayout->addWidget(m_scopeLabel);
    if (!kernel.signature.trimmed().isEmpty())
        contentLayout->addWidget(m_signatureLabel);

    m_tabs = new QTabWidget(contentHost);
    m_tabs->setObjectName(QStringLiteral("AieKernelPreviewTabs"));

    auto* codeTab = new QWidget(contentHost);
    auto* codeTabLayout = new QVBoxLayout(codeTab);
    codeTabLayout->setContentsMargins(0, 0, 0, 0);
    codeTabLayout->setSpacing(0);

    auto* metadataTab = new QWidget(contentHost);
    auto* metadataTabLayout = new QVBoxLayout(metadataTab);
    metadataTabLayout->setContentsMargins(0, 0, 0, 0);
    metadataTabLayout->setSpacing(0);

    m_codeView = new QTextEdit(codeTab);
    m_metadataView = new QTextEdit(metadataTab);
    m_codeView->setReadOnly(true);
    m_metadataView->setReadOnly(true);
    codeTabLayout->addWidget(m_codeView);
    metadataTabLayout->addWidget(m_metadataView);

    m_tabs->addTab(codeTab, QStringLiteral("Code"));
    m_tabs->addTab(metadataTab, QStringLiteral("Metadata"));
    contentLayout->addWidget(m_tabs, 1);

    auto* actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    m_openInEditorButton = new QPushButton(QStringLiteral("Open In Code Editor"), this);
    m_openInEditorButton->setObjectName(QStringLiteral("AieKernelPreviewPrimaryButton"));
    m_copyToWorkspaceButton = new QPushButton(QStringLiteral("Create Workspace Copy"), this);
    m_copyToWorkspaceButton->setObjectName(QStringLiteral("AieKernelPreviewSecondaryButton"));
    m_copyToGlobalButton = new QPushButton(QStringLiteral("Create Global Copy"), this);
    m_copyToGlobalButton->setObjectName(QStringLiteral("AieKernelPreviewSecondaryButton"));

    actionRow->addWidget(m_openInEditorButton);
    actionRow->addWidget(m_copyToWorkspaceButton);
    actionRow->addWidget(m_copyToGlobalButton);
    actionRow->addStretch(1);

    contentLayout->addLayout(actionRow);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, contentHost);
    buttonBox->setObjectName(QStringLiteral("AieKernelPreviewButtonBox"));
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (QPushButton* closeButton = buttonBox->button(QDialogButtonBox::Close))
        closeButton->setObjectName(QStringLiteral("AieKernelPreviewDangerButton"));
    contentLayout->addWidget(buttonBox);

    if (kernel.scope == KernelSourceScope::Workspace)
        m_copyToWorkspaceButton->setEnabled(false);
    if (kernel.scope == KernelSourceScope::Global)
        m_copyToGlobalButton->setEnabled(false);
}

bool KernelPreviewDialog::eventFilter(QObject* watched, QEvent* event)
{
    const bool chromeHit = (watched == m_windowChrome || watched == m_windowTitleLabel);
    if (!chromeHit || !event)
        return QDialog::eventFilter(watched, event);

    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_draggingWindow = true;
                m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (!m_draggingWindow || isMaximized())
                break;
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_draggingWindow = false;
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                toggleMaximizeRestore();
                return true;
            }
            break;
        }
        default:
            break;
    }

    return QDialog::eventFilter(watched, event);
}

void KernelPreviewDialog::toggleMaximizeRestore()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void KernelPreviewDialog::setCodeText(const QString& codeText)
{
    if (!m_codeView)
        return;

    m_codeView->setPlainText(codeText);
}

void KernelPreviewDialog::setCodeWidget(QWidget* codeWidget)
{
    if (!m_tabs || !codeWidget)
        return;

    QWidget* codeTab = m_tabs->widget(0);
    if (!codeTab || !codeTab->layout())
        return;

    QLayout* layout = codeTab->layout();
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (!item)
            continue;
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    codeWidget->setParent(codeTab);
    layout->addWidget(codeWidget);
    m_codeView = nullptr;
}

void KernelPreviewDialog::setMetadataText(const QString& metadataText)
{
    if (!m_metadataView)
        return;

    m_metadataView->setPlainText(metadataText);
}

void KernelPreviewDialog::setMetadataWidget(QWidget* metadataWidget)
{
    if (!m_tabs || !metadataWidget)
        return;

    QWidget* metadataTab = m_tabs->widget(1);
    if (!metadataTab || !metadataTab->layout())
        return;

    QLayout* layout = metadataTab->layout();
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (!item)
            continue;
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    metadataWidget->setParent(metadataTab);
    layout->addWidget(metadataWidget);
    m_metadataView = nullptr;
}

void KernelPreviewDialog::setActiveTab(Tab tab)
{
    if (!m_tabs)
        return;

    const int index = (tab == Tab::Metadata) ? 1 : 0;
    m_tabs->setCurrentIndex(index);
}

} // namespace Aie::Internal
