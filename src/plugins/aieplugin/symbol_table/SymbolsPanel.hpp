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
class QCheckBox;
class QGridLayout;
class QSpinBox;
class QStackedWidget;
class QTableView;
class QToolButton;
QT_END_NAMESPACE

namespace Utils {
class SidebarPanelFrame;
}

namespace Aie::Internal {

class SymbolsController;
class SymbolsModel;
class SymbolsFilterModel;
class TapPreviewWidget;

class AIEPLUGIN_EXPORT SymbolsPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit SymbolsPanel(SymbolsController* controller, QWidget* parent = nullptr);

private:
    void buildUi();
    void buildEditorPages();
    void rebuildDimensionEditors(int rank);
    void rebuildTapPatternEditors();
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
    void requestTapCommit();
    void flushPendingCommit();

    void commitConstantEdits();
    void commitTypeEdits();
    void commitTapEdits();
    void deleteSelectedSymbol();
    void addTapPatternRow();
    void moveTapPatternRow(int row, int delta);
    void removeTapPatternRow(int row);

    QString selectedSymbolIdFromView() const;
    QString currentEditorName() const;
    QString currentConstantPreview() const;
    QString currentTypePreview() const;
    QString currentTapPreview() const;

    QPointer<SymbolsController> m_controller;
    QPointer<Utils::SidebarPanelFrame> m_frame;
    QPointer<QLabel> m_summaryLabel;
    QPointer<QLabel> m_detailLabel;
    QPointer<QPushButton> m_addConstantButton;
    QPointer<QPushButton> m_addTypeButton;
    QPointer<QPushButton> m_addTapButton;
    QPointer<QPushButton> m_deleteButton;
    QPointer<QComboBox> m_filterCombo;
    QPointer<QTableView> m_tableView;
    QPointer<SymbolsModel> m_model;
    QPointer<SymbolsFilterModel> m_filterModel;
    QPointer<QStackedWidget> m_editorStack;
    QPointer<QWidget> m_emptyEditorPage;
    QPointer<QGroupBox> m_constantEditorCard;
    QPointer<QGroupBox> m_typeEditorCard;
    QPointer<QWidget> m_tapEditorPage;
    QPointer<QGroupBox> m_tapEditorCard;
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
    QPointer<QLineEdit> m_tapNameEdit;
    QPointer<QSpinBox> m_tapRowsSpin;
    QPointer<QSpinBox> m_tapColsSpin;
    QPointer<QSpinBox> m_tapOffsetSpin;
    QPointer<QCheckBox> m_tapShowRepetitionsCheck;
    QPointer<QWidget> m_tapPatternsHost;
    QPointer<QGridLayout> m_tapPatternsGrid;
    QPointer<QToolButton> m_tapAddPatternButton;
    QPointer<TapPreviewWidget> m_tapPreviewWidget;
    QVector<QPointer<QLineEdit>> m_dimensionEdits;
    QVector<QPointer<QCompleter>> m_dimensionCompleters;
    QVector<QPointer<QSpinBox>> m_tapSizeSpins;
    QVector<QPointer<QSpinBox>> m_tapStrideSpins;
    QVector<QPointer<QToolButton>> m_tapMoveUpButtons;
    QVector<QPointer<QToolButton>> m_tapMoveDownButtons;
    QVector<QPointer<QToolButton>> m_tapRemoveButtons;

    enum class PendingCommitKind : uint8_t {
        None,
        Constant,
        Type,
        Tap
    };

    QTimer m_commitTimer;
    PendingCommitKind m_pendingCommit = PendingCommitKind::None;
    bool m_updatingUi = false;
};

} // namespace Aie::Internal
