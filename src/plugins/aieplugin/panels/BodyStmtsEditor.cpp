// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#include "aieplugin/panels/BodyStmtsEditor.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <array>
#include <functional>

namespace Aie::Internal {

using namespace Qt::StringLiterals;

// ─── Forward declarations ─────────────────────────────────────────────────────

class StmtListWidget;

// ─── DragHandle ───────────────────────────────────────────────────────────────

/// The ≡ grip widget. Emits dragMoved/dragReleased with global Y so the parent
/// StmtListWidget can reorder rows live during the drag.
class DragHandle : public QWidget
{
    Q_OBJECT
public:
    explicit DragHandle(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(u"BodyStmtHandle"_s);
        setFixedWidth(18);
        setCursor(Qt::SizeVerCursor);
        setMouseTracking(true);

        auto* lbl = new QLabel(u"≡"_s, this);
        lbl->setObjectName(u"BodyStmtHandle"_s);
        lbl->setAlignment(Qt::AlignCenter);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(lbl);
    }

signals:
    void dragMoved(int globalY);
    void dragReleased(int globalY);

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging) {
            emit dragMoved(static_cast<int>(e->globalPosition().y()));
            e->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_dragging && e->button() == Qt::LeftButton) {
            m_dragging = false;
            emit dragReleased(static_cast<int>(e->globalPosition().y()));
            e->accept();
        }
    }

private:
    bool m_dragging = false;
};

// ─── StmtRowWidget ────────────────────────────────────────────────────────────

/// One visual row representing a single body statement.
/// For ForLoop, a nested StmtListWidget is shown below the row header.
class StmtRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StmtRowWidget(const QStringList& kernelParams,
                           const QStringList& fifoParams,
                           QWidget* parent = nullptr);

    void loadFromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
    void setParams(const QStringList& kernelParams, const QStringList& fifoParams);

signals:
    void changed();
    void removeRequested();
    void dragMoved(StmtRowWidget* self, int globalY);
    void dragReleased(StmtRowWidget* self, int globalY);

private slots:
    void onTypeChanged(int idx);

private:
    void clearFields();
    void buildFields(const QString& type, const QJsonObject& data = {});
    void populateCombo(QComboBox* combo, const QStringList& list, const QString& currentText);

    QStringList     m_kernelParams;
    QStringList     m_fifoParams;
    QString         m_currentType;

    QComboBox*      m_typeCombo       = nullptr;
    QWidget*        m_fieldsHost      = nullptr;
    QHBoxLayout*    m_fieldsLayout    = nullptr;
    QWidget*        m_nestedContainer = nullptr;
    StmtListWidget* m_nestedList      = nullptr;

    // Per-type field pointers — only valid while that type is active
    QComboBox* m_fifoCombo     = nullptr;
    QSpinBox*  m_countSpin     = nullptr;
    QLineEdit* m_localVarEdit  = nullptr;
    QComboBox* m_kernelCombo   = nullptr;
    QLineEdit* m_argsEdit      = nullptr;
    QLineEdit* m_varEdit       = nullptr;
    QLineEdit* m_loopCountEdit = nullptr;
    QComboBox* m_targetCombo   = nullptr;
    QLineEdit* m_indexEdit     = nullptr;
    QLineEdit* m_valueEdit     = nullptr;
};

// ─── StmtListWidget ──────────────────────────────────────────────────────────

/// Vertical list of StmtRowWidgets plus an [+ Add] / [+ Add inside loop] button.
class StmtListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StmtListWidget(const QStringList& kernelParams,
                            const QStringList& fifoParams,
                            bool nested, QWidget* parent = nullptr);

    void loadFromJson(const QJsonArray& arr);
    QJsonArray toJson() const;
    void setParams(const QStringList& kernelParams, const QStringList& fifoParams);

    // Called by BodyStmtsEditor and StmtRowWidget when appending a pre-loaded row
    void appendRow(const QJsonObject& data = {});

    // Reorder the given row to whichever slot globalY maps to
    void reorderRow(StmtRowWidget* row, int globalY);

signals:
    void changed();

private:
    void removeRow(StmtRowWidget* row);

    QStringList           m_kernelParams;
    QStringList           m_fifoParams;
    bool                  m_nested;
    QVBoxLayout*          m_rowsLayout = nullptr;
    QList<StmtRowWidget*> m_rows;
};

// ─── StmtRowWidget — implementation ──────────────────────────────────────────

static QLabel* makeFieldLbl(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setObjectName(u"BodyStmtFieldLabel"_s);
    return lbl;
}

StmtRowWidget::StmtRowWidget(const QStringList& kernelParams,
                             const QStringList& fifoParams,
                             QWidget* parent)
    : QWidget(parent), m_kernelParams(kernelParams), m_fifoParams(fifoParams)
{
    setObjectName(u"BodyStmtRow"_s);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(2);

    // ── Horizontal header row ────────────────────────────────────────────────
    auto* rowWidget = new QWidget(this);
    rowWidget->setObjectName(u"BodyStmtRowInner"_s);
    auto* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(6, 4, 6, 4);
    rowLayout->setSpacing(6);

    auto* handle = new DragHandle(rowWidget);
    rowLayout->addWidget(handle);
    connect(handle, &DragHandle::dragMoved,
            this, [this](int y) { emit dragMoved(this, y); });
    connect(handle, &DragHandle::dragReleased,
            this, [this](int y) { emit dragReleased(this, y); });

    m_typeCombo = new QComboBox(rowWidget);
    m_typeCombo->setObjectName(u"BodyStmtTypeCombo"_s);
    m_typeCombo->addItem(u"ACQUIRE"_s,     u"Acquire"_s);
    m_typeCombo->addItem(u"RELEASE"_s,     u"Release"_s);
    m_typeCombo->addItem(u"FOR LOOP"_s,    u"ForLoop"_s);
    m_typeCombo->addItem(u"KERNEL CALL"_s, u"KernelCall"_s);
    m_typeCombo->addItem(u"ASSIGN"_s,      u"Assignment"_s);
    rowLayout->addWidget(m_typeCombo);

    m_fieldsHost   = new QWidget(rowWidget);
    m_fieldsLayout = new QHBoxLayout(m_fieldsHost);
    m_fieldsLayout->setContentsMargins(0, 0, 0, 0);
    m_fieldsLayout->setSpacing(6);
    rowLayout->addWidget(m_fieldsHost, 1);

    auto* removeBtn = new QPushButton(u"✕"_s, rowWidget);
    removeBtn->setObjectName(u"BodyStmtRemoveBtn"_s);
    removeBtn->setFixedSize(22, 22);
    rowLayout->addWidget(removeBtn);

    outerLayout->addWidget(rowWidget);

    // ── Nested container (ForLoop children) ─────────────────────────────────
    m_nestedContainer = new QWidget(this);
    m_nestedContainer->setObjectName(u"BodyStmtNestedContainer"_s);
    auto* nestedOuterLayout = new QHBoxLayout(m_nestedContainer);
    nestedOuterLayout->setContentsMargins(24, 2, 0, 2);
    nestedOuterLayout->setSpacing(0);
    m_nestedList = new StmtListWidget(kernelParams, fifoParams, /*nested=*/true, m_nestedContainer);
    nestedOuterLayout->addWidget(m_nestedList);
    m_nestedContainer->setVisible(false);
    outerLayout->addWidget(m_nestedContainer);

    // ── Signals ──────────────────────────────────────────────────────────────
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StmtRowWidget::onTypeChanged);
    connect(m_nestedList, &StmtListWidget::changed,
            this, &StmtRowWidget::changed);
    connect(removeBtn, &QPushButton::clicked,
            this, &StmtRowWidget::removeRequested);

    // Build default fields (Acquire)
    buildFields(u"Acquire"_s);
}

void StmtRowWidget::populateCombo(QComboBox* combo, const QStringList& list,
                                  const QString& currentText)
{
    QSignalBlocker blk(combo);
    combo->clear();
    for (const auto& p : list)
        combo->addItem(p);
    combo->setCurrentText(currentText);
}

void StmtRowWidget::setParams(const QStringList& kernelParams, const QStringList& fifoParams)
{
    m_kernelParams = kernelParams;
    m_fifoParams   = fifoParams;
    if (m_fifoCombo)   populateCombo(m_fifoCombo,   m_fifoParams,   m_fifoCombo->currentText());
    if (m_kernelCombo) populateCombo(m_kernelCombo, m_kernelParams, m_kernelCombo->currentText());
    if (m_targetCombo) populateCombo(m_targetCombo, m_fifoParams,   m_targetCombo->currentText());
    if (m_nestedList)  m_nestedList->setParams(kernelParams, fifoParams);
}

void StmtRowWidget::clearFields()
{
    m_fifoCombo    = nullptr;
    m_countSpin    = nullptr;
    m_localVarEdit = nullptr;
    m_kernelCombo  = nullptr;
    m_argsEdit     = nullptr;
    m_varEdit      = nullptr;
    m_loopCountEdit = nullptr;
    m_targetCombo  = nullptr;
    m_indexEdit    = nullptr;
    m_valueEdit    = nullptr;

    while (QLayoutItem* item = m_fieldsLayout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void StmtRowWidget::buildFields(const QString& type, const QJsonObject& data)
{
    clearFields();
    m_currentType = type;
    m_nestedContainer->setVisible(type == u"ForLoop"_s);

    auto addLbl = [&](const QString& text) {
        m_fieldsLayout->addWidget(makeFieldLbl(text, m_fieldsHost));
    };

    auto makeCombo = [&](const QStringList& list, const QString& current) -> QComboBox* {
        auto* c = new QComboBox(m_fieldsHost);
        c->setObjectName(u"BodyStmtField"_s);
        c->setEditable(true);
        populateCombo(c, list, current);
        return c;
    };

    auto makeLineEdit = [&](const QString& text, const QString& placeholder,
                            int fixedWidth = -1) -> QLineEdit* {
        auto* e = new QLineEdit(m_fieldsHost);
        e->setObjectName(u"BodyStmtField"_s);
        e->setText(text);
        e->setPlaceholderText(placeholder);
        if (fixedWidth > 0)
            e->setFixedWidth(fixedWidth);
        return e;
    };

    if (type == u"Acquire"_s) {
        addLbl(u"fifo"_s);
        m_fifoCombo = makeCombo(m_fifoParams, data.value(u"fifo_param"_s).toString());
        m_fieldsLayout->addWidget(m_fifoCombo);

        addLbl(u"count"_s);
        m_countSpin = new QSpinBox(m_fieldsHost);
        m_countSpin->setObjectName(u"BodyStmtField"_s);
        m_countSpin->setRange(1, 9999);
        m_countSpin->setValue(data.value(u"count"_s).toInt(1));
        m_countSpin->setFixedWidth(60);
        m_fieldsLayout->addWidget(m_countSpin);

        addLbl(u"var"_s);
        m_localVarEdit = makeLineEdit(data.value(u"local_var"_s).toString(), u"buf"_s);
        m_fieldsLayout->addWidget(m_localVarEdit, 1);

        connect(m_fifoCombo,   &QComboBox::currentTextChanged, this, &StmtRowWidget::changed);
        connect(m_countSpin,   QOverload<int>::of(&QSpinBox::valueChanged), this, &StmtRowWidget::changed);
        connect(m_localVarEdit, &QLineEdit::textChanged, this, &StmtRowWidget::changed);

    } else if (type == u"Release"_s) {
        addLbl(u"fifo"_s);
        m_fifoCombo = makeCombo(m_fifoParams, data.value(u"fifo_param"_s).toString());
        m_fieldsLayout->addWidget(m_fifoCombo);

        addLbl(u"count"_s);
        m_countSpin = new QSpinBox(m_fieldsHost);
        m_countSpin->setObjectName(u"BodyStmtField"_s);
        m_countSpin->setRange(1, 9999);
        m_countSpin->setValue(data.value(u"count"_s).toInt(1));
        m_countSpin->setFixedWidth(60);
        m_fieldsLayout->addWidget(m_countSpin);
        m_fieldsLayout->addStretch(1);

        connect(m_fifoCombo, &QComboBox::currentTextChanged, this, &StmtRowWidget::changed);
        connect(m_countSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &StmtRowWidget::changed);

    } else if (type == u"ForLoop"_s) {
        addLbl(u"var"_s);
        m_varEdit = makeLineEdit(data.value(u"var"_s).toString(), u"i"_s, 50);
        m_fieldsLayout->addWidget(m_varEdit);

        addLbl(u"count"_s);
        m_loopCountEdit = makeLineEdit(data.value(u"count"_s).toString(), u"N"_s);
        m_fieldsLayout->addWidget(m_loopCountEdit, 1);

        connect(m_varEdit,      &QLineEdit::textChanged, this, &StmtRowWidget::changed);
        connect(m_loopCountEdit, &QLineEdit::textChanged, this, &StmtRowWidget::changed);

        if (data.contains(u"body"_s))
            m_nestedList->loadFromJson(data.value(u"body"_s).toArray());

    } else if (type == u"KernelCall"_s) {
        addLbl(u"kernel"_s);
        m_kernelCombo = makeCombo(m_kernelParams, data.value(u"kernel_param"_s).toString());
        m_fieldsLayout->addWidget(m_kernelCombo);

        addLbl(u"args"_s);
        QStringList argsList;
        for (const auto& a : data.value(u"args"_s).toArray())
            argsList.append(a.toString());
        m_argsEdit = makeLineEdit(argsList.join(u", "_s), u"arg1, arg2"_s);
        m_fieldsLayout->addWidget(m_argsEdit, 1);

        connect(m_kernelCombo, &QComboBox::currentTextChanged, this, &StmtRowWidget::changed);
        connect(m_argsEdit,    &QLineEdit::textChanged,        this, &StmtRowWidget::changed);

    } else if (type == u"Assignment"_s) {
        addLbl(u"target"_s);
        m_targetCombo = makeCombo(m_fifoParams, data.value(u"target"_s).toString());
        m_fieldsLayout->addWidget(m_targetCombo);

        addLbl(u"index"_s);
        m_indexEdit = makeLineEdit(data.value(u"index"_s).toString(), u"i"_s, 50);
        m_fieldsLayout->addWidget(m_indexEdit);

        addLbl(u"value"_s);
        const QJsonValue valJson = data.value(u"value"_s);
        const QString valStr = valJson.isDouble()
            ? QString::number(static_cast<int>(valJson.toDouble()))
            : valJson.toString();
        m_valueEdit = makeLineEdit(valStr, u"0"_s);
        m_fieldsLayout->addWidget(m_valueEdit, 1);

        connect(m_targetCombo, &QComboBox::currentTextChanged, this, &StmtRowWidget::changed);
        connect(m_indexEdit,   &QLineEdit::textChanged,        this, &StmtRowWidget::changed);
        connect(m_valueEdit,   &QLineEdit::textChanged,        this, &StmtRowWidget::changed);
    }
}

void StmtRowWidget::onTypeChanged(int /*idx*/)
{
    buildFields(m_typeCombo->currentData().toString());
    emit changed();
}

void StmtRowWidget::loadFromJson(const QJsonObject& obj)
{
    const QString type = obj.value(u"type"_s).toString();
    for (int i = 0; i < m_typeCombo->count(); ++i) {
        if (m_typeCombo->itemData(i).toString() == type) {
            QSignalBlocker blk(m_typeCombo);
            m_typeCombo->setCurrentIndex(i);
            break;
        }
    }
    buildFields(type, obj);
}

QJsonObject StmtRowWidget::toJson() const
{
    QJsonObject obj;
    obj.insert(u"type"_s, m_currentType);

    if (m_currentType == u"Acquire"_s) {
        obj.insert(u"fifo_param"_s, m_fifoCombo    ? m_fifoCombo->currentText()    : QString{});
        obj.insert(u"count"_s,      m_countSpin    ? m_countSpin->value()          : 1);
        obj.insert(u"local_var"_s,  m_localVarEdit ? m_localVarEdit->text()        : QString{});

    } else if (m_currentType == u"Release"_s) {
        obj.insert(u"fifo_param"_s, m_fifoCombo ? m_fifoCombo->currentText() : QString{});
        obj.insert(u"count"_s,      m_countSpin ? m_countSpin->value()       : 1);

    } else if (m_currentType == u"ForLoop"_s) {
        obj.insert(u"var"_s,   m_varEdit       ? m_varEdit->text()       : QString{});
        obj.insert(u"count"_s, m_loopCountEdit ? m_loopCountEdit->text() : QString{});
        obj.insert(u"body"_s,  m_nestedList    ? m_nestedList->toJson()  : QJsonArray{});

    } else if (m_currentType == u"KernelCall"_s) {
        obj.insert(u"kernel_param"_s, m_kernelCombo ? m_kernelCombo->currentText() : QString{});
        QJsonArray argsArr;
        if (m_argsEdit) {
            for (const auto& part : m_argsEdit->text().split(u',', Qt::SkipEmptyParts))
                argsArr.append(part.trimmed());
        }
        obj.insert(u"args"_s, argsArr);

    } else if (m_currentType == u"Assignment"_s) {
        obj.insert(u"target"_s, m_targetCombo ? m_targetCombo->currentText() : QString{});
        obj.insert(u"index"_s,  m_indexEdit   ? m_indexEdit->text()          : QString{});
        if (m_valueEdit) {
            bool ok = false;
            const int ival = m_valueEdit->text().toInt(&ok);
            if (ok) obj.insert(u"value"_s, ival);
            else    obj.insert(u"value"_s, m_valueEdit->text());
        }
    }
    return obj;
}

// ─── StmtListWidget — implementation ─────────────────────────────────────────

StmtListWidget::StmtListWidget(const QStringList& kernelParams,
                               const QStringList& fifoParams,
                               bool nested, QWidget* parent)
    : QWidget(parent), m_kernelParams(kernelParams), m_fifoParams(fifoParams), m_nested(nested)
{
    setObjectName(nested ? u"BodyStmtNestedList"_s : u"BodyStmtList"_s);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(4);

    auto* rowsHost = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(4);
    outerLayout->addWidget(rowsHost);

    auto* addBtn = new QPushButton(nested ? u"+ Add inside loop"_s : u"+ Add"_s, this);
    addBtn->setObjectName(u"BodyStmtAddBtn"_s);
    outerLayout->addWidget(addBtn, 0, Qt::AlignLeft);

    connect(addBtn, &QPushButton::clicked, this, [this]() { appendRow(); });
}

void StmtListWidget::appendRow(const QJsonObject& data)
{
    auto* row = new StmtRowWidget(m_kernelParams, m_fifoParams, this);
    if (!data.isEmpty())
        row->loadFromJson(data);

    connect(row, &StmtRowWidget::changed,
            this, &StmtListWidget::changed);
    connect(row, &StmtRowWidget::removeRequested,
            this, [this, row]() { removeRow(row); });
    connect(row, &StmtRowWidget::dragMoved,
            this, [this](StmtRowWidget* r, int y) { reorderRow(r, y); });

    m_rows.append(row);
    m_rowsLayout->addWidget(row);
    emit changed();
}

void StmtListWidget::reorderRow(StmtRowWidget* row, int globalY)
{
    const int oldIdx = m_rows.indexOf(row);
    if (oldIdx < 0)
        return;

    // Determine target slot: find first row whose centre is below globalY
    int targetIdx = m_rows.size(); // default: end
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i] == row)
            continue;
        const QRect geom = m_rows[i]->geometry();
        // Map the centre of this row to global coords via the rows host widget
        const QPoint centre = m_rows[i]->parentWidget()
                                  ->mapToGlobal(QPoint(0, geom.top() + geom.height() / 2));
        if (globalY < centre.y()) {
            targetIdx = i;
            break;
        }
    }

    // Clamp and skip no-op
    targetIdx = qBound(0, targetIdx, m_rows.size() - 1);
    if (targetIdx == oldIdx)
        return;

    m_rows.removeAt(oldIdx);
    m_rowsLayout->removeWidget(row);

    m_rows.insert(targetIdx, row);
    m_rowsLayout->insertWidget(targetIdx, row);

    emit changed();
}

void StmtListWidget::removeRow(StmtRowWidget* row)
{
    m_rows.removeOne(row);
    m_rowsLayout->removeWidget(row);
    row->deleteLater();
    emit changed();
}

void StmtListWidget::loadFromJson(const QJsonArray& arr)
{
    for (auto* row : m_rows) {
        m_rowsLayout->removeWidget(row);
        row->deleteLater();
    }
    m_rows.clear();

    for (const auto& val : arr) {
        if (val.isObject())
            appendRow(val.toObject());
    }
}

QJsonArray StmtListWidget::toJson() const
{
    QJsonArray arr;
    for (const auto* row : m_rows)
        arr.append(row->toJson());
    return arr;
}

void StmtListWidget::setParams(const QStringList& kernelParams, const QStringList& fifoParams)
{
    m_kernelParams = kernelParams;
    m_fifoParams   = fifoParams;
    for (auto* row : m_rows)
        row->setParams(kernelParams, fifoParams);
}

// ─── BodyStmtsEditor — implementation ────────────────────────────────────────

static void clearChipsLayout(QHBoxLayout* layout)
{
    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

static void populateChips(QHBoxLayout* layout, QWidget* chipsRow,
                           const QStringList& params, int bucket,
                           BodyStmtsEditor* editor)
{
    clearChipsLayout(layout);
    for (const QString& param : params) {
        auto* chip = new QWidget(chipsRow);
        chip->setObjectName(u"BodyStmtParamChip"_s);
        auto* chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(6, 2, 4, 2);
        chipLayout->setSpacing(3);

        auto* chipLabel = new QLabel(param, chip);
        chipLabel->setObjectName(u"BodyStmtParamChipLabel"_s);
        chipLayout->addWidget(chipLabel);

        auto* removeBtn = new QPushButton(u"×"_s, chip);
        removeBtn->setObjectName(u"BodyStmtParamChipRemove"_s);
        removeBtn->setFixedSize(14, 14);
        removeBtn->setFlat(true);
        QObject::connect(removeBtn, &QPushButton::clicked, editor,
                         [editor, param, bucket]() { editor->removeParam(param, bucket); });
        chipLayout->addWidget(removeBtn);

        layout->addWidget(chip);
    }
    layout->addStretch(1);
}

BodyStmtsEditor::BodyStmtsEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(4);

    static const std::array<QString, 3> kLabels    = {u"Kernel"_s,  u"Inputs"_s,  u"Outputs"_s};
    static const std::array<QString, 3> kAddTips   = {u"Add kernel parameter"_s,
                                                       u"Add input FIFO parameter"_s,
                                                       u"Add output FIFO parameter"_s};
    static const std::array<QString, 3> kDialogTitles = {u"Add Kernel Parameter"_s,
                                                          u"Add Input Parameter"_s,
                                                          u"Add Output Parameter"_s};

    for (int b = 0; b < 3; ++b) {
        auto* row = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto* lbl = new QLabel(kLabels[b], row);
        lbl->setObjectName(u"AiePropertiesKeyLabel"_s);
        lbl->setFixedWidth(50);
        rowLayout->addWidget(lbl);

        m_chipsRow[b] = new QWidget(row);
        m_chipsLayout[b] = new QHBoxLayout(m_chipsRow[b]);
        m_chipsLayout[b]->setContentsMargins(0, 0, 0, 0);
        m_chipsLayout[b]->setSpacing(4);
        m_chipsLayout[b]->addStretch(1);
        rowLayout->addWidget(m_chipsRow[b], 1);

        auto* addBtn = new QPushButton(u"+"_s, row);
        addBtn->setObjectName(u"BodyStmtAddParamBtn"_s);
        addBtn->setFixedSize(22, 22);
        addBtn->setToolTip(kAddTips[b]);
        rowLayout->addWidget(addBtn);

        const QString dialogTitle = kDialogTitles[b];
        connect(addBtn, &QPushButton::clicked, this, [this, b, dialogTitle]() {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, dialogTitle, u"Parameter name:"_s,
                QLineEdit::Normal, {}, &ok);
            if (ok && !name.trimmed().isEmpty())
                addParam(name.trimmed(), static_cast<Bucket>(b));
        });

        outerLayout->addWidget(row);
    }

    // ── Statement list ───────────────────────────────────────────────────────
    m_stmtList = new StmtListWidget({}, {}, /*nested=*/false, this);
    outerLayout->addWidget(m_stmtList);

    connect(m_stmtList, &StmtListWidget::changed, this, &BodyStmtsEditor::onChanged);

    rebuildParamChips();
}

void BodyStmtsEditor::addParam(const QString& name, Bucket bucket)
{
    for (const auto& list : m_params)
        if (list.contains(name)) return;
    m_params[bucket].append(name);
    rebuildParamChips();
    m_stmtList->setParams(m_params[Kernel], m_params[Input] + m_params[Output]);
    onChanged();
}

void BodyStmtsEditor::removeParam(const QString& name, int bucket)
{
    const int idx = m_params[bucket].indexOf(name);
    if (idx < 0) return;
    m_params[bucket].removeAt(idx);
    rebuildParamChips();
    m_stmtList->setParams(m_params[Kernel], m_params[Input] + m_params[Output]);
    onChanged();
}

QStringList BodyStmtsEditor::allParams() const
{
    // Order: kernels, inputs, outputs — matches fn_args convention
    return m_params[Kernel] + m_params[Input] + m_params[Output];
}

void BodyStmtsEditor::rebuildParamChips()
{
    for (int b = 0; b < 3; ++b)
        populateChips(m_chipsLayout[b], m_chipsRow[b], m_params[b], b, this);
}

void BodyStmtsEditor::setJson(const QString& json)
{
    const QString trimmed = json.trimmed();

    QStringList flatParams;
    QJsonObject paramRoles; // optional: name → bucket index (0=kernel,1=input,2=output)
    QJsonArray  body;

    if (!trimmed.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (const auto& p : obj.value(u"params"_s).toArray())
                flatParams.append(p.toString());
            paramRoles = obj.value(u"param_roles"_s).toObject();
            body = obj.value(u"body"_s).toArray();
        } else if (doc.isArray()) {
            body = doc.array();
            std::function<void(const QJsonArray&)> walk = [&](const QJsonArray& arr) {
                for (const auto& v : arr) {
                    if (!v.isObject()) continue;
                    const QJsonObject s = v.toObject();
                    auto collect = [&](const QString& key) {
                        const QString val = s.value(key).toString();
                        if (!val.isEmpty() && !flatParams.contains(val))
                            flatParams.append(val);
                    };
                    collect(u"kernel_param"_s);
                    collect(u"fifo_param"_s);
                    if (s.contains(u"body"_s))
                        walk(s.value(u"body"_s).toArray());
                }
            };
            walk(body);
        }
    }

    for (auto& list : m_params) list.clear();

    if (!paramRoles.isEmpty()) {
        // Authoritative bucket info saved by toJson() — use it directly.
        for (const QString& p : flatParams) {
            const int bucket = qBound(0, paramRoles.value(p).toInt(Input), 2);
            m_params[bucket].append(p);
        }
    } else {
        // Legacy / hand-written JSON: infer from kernel_param references.
        // Kernel params → Kernel; everything else → Input (user can reassign via chips).
        QSet<QString> kernelRefs;
        std::function<void(const QJsonArray&)> classify = [&](const QJsonArray& arr) {
            for (const auto& v : arr) {
                if (!v.isObject()) continue;
                const QJsonObject s = v.toObject();
                const QString kp = s.value(u"kernel_param"_s).toString();
                if (!kp.isEmpty()) kernelRefs.insert(kp);
                if (s.contains(u"body"_s))
                    classify(s.value(u"body"_s).toArray());
            }
        };
        classify(body);
        for (const QString& p : flatParams) {
            if (kernelRefs.contains(p)) m_params[Kernel].append(p);
            else                        m_params[Input].append(p);
        }
    }

    rebuildParamChips();
    m_stmtList->setParams(m_params[Kernel], m_params[Input] + m_params[Output]);
    m_stmtList->loadFromJson(body);
}

bool BodyStmtsEditor::isEmpty() const
{
    for (const auto& list : m_params)
        if (!list.isEmpty()) return false;
    return m_stmtList->toJson().isEmpty();
}

QString BodyStmtsEditor::toJson() const
{
    QJsonObject root;
    QJsonArray  paramsArr;
    QJsonObject paramRoles;
    for (int b = 0; b < 3; ++b) {
        for (const auto& p : m_params[b]) {
            paramsArr.append(p);
            paramRoles.insert(p, b);
        }
    }
    root.insert(u"params"_s, paramsArr);
    root.insert(u"param_roles"_s, paramRoles);
    root.insert(u"body"_s, m_stmtList->toJson());
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BodyStmtsEditor::onChanged()
{
    emit jsonChanged(toJson());
}

} // namespace Aie::Internal

#include "BodyStmtsEditor.moc"
