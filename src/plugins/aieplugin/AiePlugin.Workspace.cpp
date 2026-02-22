// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#if !defined(AIEPLUGIN_INTERNAL_IMPL)
#error "AiePlugin.Workspace.cpp is an internal implementation fragment. Include from AiePlugin.cpp."
#endif

#include "aieplugin/design/DesignOpenController.hpp"
#include "aieplugin/kernels/KernelRegistryService.hpp"

#include <utils/DocumentBundle.hpp>
#include <utils/PathUtils.hpp>

#include "projectexplorer/api/IProjectExplorer.hpp"

#include <QtCore/QFileInfo>

namespace Aie::Internal {

void AiePlugin::configureWorkspacePersistence(const RuntimeDependencies& deps)
{
    if (!m_designOpenController)
        return;

    connect(m_designOpenController, &DesignOpenController::designOpened, this,
            [this](const QString& bundlePath, const QString&, const QString&) {
                if (m_workspaceRoot.isEmpty())
                    return;
                m_workspaceState.setActiveBundlePathForRoot(m_workspaceRoot, bundlePath);
            });

    m_workspaceRoot = Utils::PathUtils::normalizePath(deps.projectExplorer ? deps.projectExplorer->rootPath()
                                                                            : QString());

    if (m_kernelRegistry) {
        m_kernelRegistry->setWorkspaceRoot(m_workspaceRoot);
        const Utils::Result reloadResult = m_kernelRegistry->reload();
        if (!reloadResult) {
            logResultErrors(QStringLiteral("AiePlugin: failed to reload kernel registry for workspace root."),
                            reloadResult);
        }
    }

    if (deps.projectExplorer) {
        connect(deps.projectExplorer,
                &ProjectExplorer::IProjectExplorer::workspaceRootChanged,
                this,
                [this](const QString& rootPath, bool userInitiated) {
                    Q_UNUSED(userInitiated);

                    if (!m_workspaceRoot.isEmpty() && m_designOpenController) {
                        const QString activeBundlePath = m_designOpenController->activeBundlePath();
                        if (!activeBundlePath.isEmpty()) {
                            m_workspaceState.setActiveBundlePathForRoot(m_workspaceRoot,
                                                                        activeBundlePath);
                        }
                    }

                    m_workspaceRoot = Utils::PathUtils::normalizePath(rootPath);

                    if (m_kernelRegistry) {
                        m_kernelRegistry->setWorkspaceRoot(m_workspaceRoot);
                        const Utils::Result reloadResult = m_kernelRegistry->reload();
                        if (!reloadResult) {
                            logResultErrors(
                                QStringLiteral("AiePlugin: failed to reload kernel registry after workspace change."),
                                reloadResult);
                        }
                    }

                    restoreWorkspaceDesign();
                });
    }

    restoreWorkspaceDesign();
}

void AiePlugin::restoreWorkspaceDesign()
{
    if (m_workspaceRoot.isEmpty() || !m_designOpenController)
        return;

    const QString persistedBundlePath = m_workspaceState.activeBundlePathForRoot(m_workspaceRoot);
    if (persistedBundlePath.isEmpty())
        return;

    if (!isRestorableBundlePath(persistedBundlePath, m_workspaceRoot)) {
        m_workspaceState.clearRoot(m_workspaceRoot);
        return;
    }

    const QString activeBundlePath = Utils::PathUtils::normalizePath(m_designOpenController->activeBundlePath());
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    if (!activeBundlePath.isEmpty() && QString::compare(activeBundlePath, persistedBundlePath, pathCase) == 0)
        return;

    m_designOpenController->openBundlePath(persistedBundlePath);
}

bool AiePlugin::isRestorableBundlePath(const QString& bundlePath, const QString& workspaceRoot) const
{
    const QString normalizedBundlePath = Utils::PathUtils::normalizePath(bundlePath);
    const QString normalizedWorkspaceRoot = Utils::PathUtils::normalizePath(workspaceRoot);
    if (normalizedBundlePath.isEmpty() || normalizedWorkspaceRoot.isEmpty())
        return false;

    if (!Utils::DocumentBundle::hasBundleExtension(normalizedBundlePath))
        return false;

    const QFileInfo bundleInfo(normalizedBundlePath);
    if (!bundleInfo.exists() || !bundleInfo.isDir())
        return false;

    QString workspacePrefix = normalizedWorkspaceRoot;
    if (!workspacePrefix.endsWith('/'))
        workspacePrefix.append('/');

#if defined(Q_OS_WIN)
    return normalizedBundlePath.startsWith(workspacePrefix, Qt::CaseInsensitive);
#else
    return normalizedBundlePath.startsWith(workspacePrefix, Qt::CaseSensitive);
#endif
}

} // namespace Aie::Internal