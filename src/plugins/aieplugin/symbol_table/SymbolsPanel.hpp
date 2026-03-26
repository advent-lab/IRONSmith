// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "aieplugin/AieGlobal.hpp"

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVector>

#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QCompleter;
class QFormLayout;
class QGroupBox;
class QHeaderView;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableView;
QT_END_NAMESPACE

namespace Utils {
class SidebarPanelFrame;
}

namespace Aie::Internal {

class SymbolsController;
class SymbolsModel;
class SymbolsFilterModel;

class AIEPLUGIN_EXPORT SymbolsPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit SymbolsPanel(SymbolsController* controller, QWidget* parent = nullptr);

private:
    void buildUi();
    void buildEditorPages();
    void rebuildDimensionEditors(int rank);
    void bindController();
    void refreshPanelState();
    void refreshSelection();
    void refreshEditor();
    void refreshEditorPreview();
    void refreshStatusMessage(const QString& message = QString(), bool error = false);
    void updateSummaryText();
    void updateActionState();
    void requestConstantCommit();
    void requestTypeCommit();
    void flushPendingCommit();

    void commitConstantEdits();
    void commitTypeEdits();
    void deleteSelectedSymbol();

    QString selectedSymbolIdFromView() const;
    QString currentEditorName() const;
    QString currentConstantPreview() const;
    QString currentTypePreview() const;

    QPointer<SymbolsController> m_controller;
    QPointer<Utils::SidebarPanelFrame> m_frame;
    QPointer<QLabel> m_summaryLabel;
    QPointer<QLabel> m_detailLabel;
    QPointer<QPushButton> m_addConstantButton;
    QPointer<QPushButton> m_addTypeButton;
    QPointer<QPushButton> m_deleteButton;
    QPointer<QComboBox> m_filterCombo;
    QPointer<QTableView> m_tableView;
    QPointer<SymbolsModel> m_model;
    QPointer<SymbolsFilterModel> m_filterModel;
    QPointer<QStackedWidget> m_editorStack;
    QPointer<QWidget> m_emptyEditorPage;
    QPointer<QGroupBox> m_constantEditorCard;
    QPointer<QGroupBox> m_typeEditorCard;
    QPointer<QLineEdit> m_constantNameEdit;
    QPointer<QLineEdit> m_constantValueEdit;
    QPointer<QLabel> m_constantReferencesLabel;
    QPointer<QLabel> m_constantPreviewLabel;
    QPointer<QLineEdit> m_typeNameEdit;
    QPointer<QLabel> m_typeContainerValue;
    QPointer<QSpinBox> m_typeRankSpin;
    QPointer<QFormLayout> m_typeDimensionsForm;
    QPointer<QComboBox> m_typeDTypeCombo;
    QPointer<QLabel> m_typePreviewLabel;
    QVector<QPointer<QLineEdit>> m_dimensionEdits;
    QVector<QPointer<QCompleter>> m_dimensionCompleters;

    enum class PendingCommitKind : uint8_t {
        None,
        Constant,
        Type
    };

    QTimer m_commitTimer;
    PendingCommitKind m_pendingCommit = PendingCommitKind::None;
    bool m_updatingUi = false;
};

} // namespace Aie::Internal
