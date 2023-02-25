/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "vme_config_ui_variable_editor.h"

#include <boost/range/adaptor/indexed.hpp>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>

#include <QIntValidator>

#include <QDebug>

#include "qt_util.h"

using boost::adaptors::indexed;

namespace
{
constexpr int OriginalVariableNameRole = Qt::UserRole+1;

std::unique_ptr<QStandardItem> make_item(const QString &text, bool isSysVar)
{
    auto sti = std::make_unique<QStandardItem>(text);

    if (isSysVar)
    {
        sti->setFlags(sti->flags() & ~Qt::ItemIsEditable);
        sti->setBackground(QBrush(Qt::lightGray, Qt::BDiagPattern));
    }

    return sti;
}

void populate_model(QStandardItemModel &model, const vme_script::SymbolTable &symtab)
{
    model.clear();
    model.setColumnCount(3);
    model.setHorizontalHeaderLabels({"Variable Name", "Variable Value", "Comment"});

    model.setRowCount(symtab.size());

    auto symbolNames = symtab.symbolNames();
    std::sort(symbolNames.begin(), symbolNames.end(), vme_script::variable_name_cmp_sys_first);

    for (const auto &nameAndIndex: symbolNames | indexed(0))
    {
        auto row = nameAndIndex.index();
        const auto &name = nameAndIndex.value();
        const auto &var = symtab[name];
        const bool isSysVar = vme_script::is_system_variable_name(name);

        auto sti = make_item(name, isSysVar);
        sti->setData(name, OriginalVariableNameRole);
        model.setItem(row, 0, sti.release());

        sti = make_item(var.value, isSysVar);
        model.setItem(row, 1, sti.release());

        sti = make_item(var.comment, isSysVar);
        model.setItem(row, 2, sti.release());
    }
}

namespace
{

struct VarModelInfo
{
    QString name;
    QString originalName;
    vme_script::Variable var;
};

VarModelInfo get_variable_model_info(const QStandardItemModel &model, int row)
{
    assert(model.columnCount() == 3);

    if (0 > row || row >= model.rowCount())
        return {};

    auto name = model.item(row, 0)->text();
    auto value = model.item(row, 1)->text();
    auto comment = model.item(row, 2)->text();
    auto originalName = model.item(row, 0)->data(OriginalVariableNameRole).toString();

    vme_script::Variable var;
    var.value = value;
    var.comment = comment;

    return { name, originalName, var };
}

} // end anon namespace

void save_to_symboltable(const QStandardItemModel &model, vme_script::SymbolTable &symtab)
{
    for (int row = 0; row < model.rowCount(); row++)
    {
        auto varInfo = get_variable_model_info(model, row);

        symtab[varInfo.name] = varInfo.var;
    }
}

void set_variable_value(QStandardItemModel &model, const QString &varName, const QString &varValue)
{
    for (int row = 0; row < model.rowCount(); row++)
    {
        assert(model.columnCount() == 3);
        auto name = model.item(row, 0)->text();

        if (varName == name)
        {
            model.item(row, 1)->setText(varValue);
            return;
        }
    }

    const bool isSysVar = vme_script::is_system_variable_name(varName);

    QList<QStandardItem *> items;
    items.push_back(make_item(varName, isSysVar).release());
    items.push_back(make_item(varValue, isSysVar).release());
    items.push_back(make_item({}, isSysVar).release());
    model.appendRow(items);
}

QStringList get_symbol_names(const QStandardItemModel &model)
{
    // Instead of using save_to_symboltable() and returning the symbolNames
    // from the table this function manually walks through the rows and
    // collects the names.
    // This way the names position in the list corresponds to it's row in the
    // model.
    assert(model.columnCount() == 3);

    QStringList ret;

    for (int row = 0; row < model.rowCount(); row++)
    {
        ret.push_back(model.item(row, 0)->text());
    }

    return ret;
}

struct VariableNameValidator: public QValidator
{
    public:
        VariableNameValidator(const QStringList &existingVarNames, QObject *parent = nullptr)
            : QValidator(parent)
            , m_existingNames(existingVarNames)
        {}

        QValidator::State validate(QString &input, int &/*pos*/) const override
        {
            if (input.contains(' '))
            {
                qDebug() << __PRETTY_FUNCTION__ << "contains spaces -> Invalid";
                return QValidator::Invalid;
            }

            if (vme_script::is_system_variable_name(input))
            {
                qDebug() << __PRETTY_FUNCTION__ << "is_system_variable_name -> Invalid";
                return QValidator::Invalid;
            }

            if (m_existingNames.contains(input))
            {
                qDebug() << __PRETTY_FUNCTION__ << "duplicate var name -> Intermediate";
                return QValidator::Intermediate;
            }

            if (!input.isEmpty())
            {
                qDebug() << __PRETTY_FUNCTION__ << "non emtpy -> acceptable";
                return QValidator::Acceptable;
            }

            qDebug() << __PRETTY_FUNCTION__ << "else -> Intermediate";
            return QValidator::Intermediate;
        }

    private:
        QStringList m_existingNames;
};

struct VariableNameEditorDelegate: public QStyledItemDelegate
{
    public:
        VariableNameEditorDelegate(QStandardItemModel *model, QObject *parent = 0)
            : QStyledItemDelegate(parent)
            , m_model(model)
        {}

        virtual QWidget* createEditor(QWidget *parent,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const override
        {
            assert(index.column() == 0);
            assert(m_model);

            auto editor = QStyledItemDelegate::createEditor(parent, option, index);

            if (auto le = qobject_cast<QLineEdit *>(editor))
            {
                auto existingNames = get_symbol_names(*m_model);
                existingNames.removeAt(index.row()); // remove the name in the row currently being edited

                qDebug() << __PRETTY_FUNCTION__ << editor << "isLineEdit, existingNames=" << existingNames;
                auto validator = new VariableNameValidator(existingNames, le);
                le->setValidator(validator);
            }

            return editor;
        }

    private:
        QStandardItemModel *m_model;
};

} // end anon namespace

struct VariableEditorWidget::Private
{
    QTableView *tableView;
    std::unique_ptr<QStandardItemModel> model;

    // The symbol table currently stored in the model. Used to detect when
    // variable values are modified by the user.
    vme_script::SymbolTable modelSymtab;

    QPushButton *pb_addVariable,
                *pb_delVariable;

    // Used to suppress signal emission during certain modifications to the
    // model like repopulating and adding/removing variables.
    bool isModifying;
};

// FIXME: VariableEditorWidget: by pressing ESC or clicking anywhere in the
// table duplicate variables can still be created.
// FIXME: adding multiple variables with the same name is also possible by just
// clicking the add button multiple times.

VariableEditorWidget::VariableEditorWidget(
    QWidget *parent)
: QWidget(parent)
, d(std::make_unique<Private>())
{
    *d = {};
    d->model = std::make_unique<QStandardItemModel>();
    d->model->setColumnCount(3);

    d->tableView = new QTableView(this);
    d->tableView->verticalHeader()->hide();
    d->tableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    d->tableView->horizontalHeader()->setStretchLastSection(true);
    d->tableView->setSelectionMode(QAbstractItemView::SingleSelection);

    auto nameDelegate = new VariableNameEditorDelegate(d->model.get(), this);
    d->tableView->setItemDelegateForColumn(0, nameDelegate);

    d->pb_addVariable = new QPushButton(QIcon(":/list_add.png"), "&Add new variable");
    d->pb_delVariable = new QPushButton(QIcon(":/list_remove.png"), "&Delete selected variable");
    d->pb_delVariable->setEnabled(false);

    auto actionButtonsLayout = make_hbox();
    actionButtonsLayout->addWidget(d->pb_addVariable);
    actionButtonsLayout->addWidget(d->pb_delVariable);
    actionButtonsLayout->addStretch(1);

    auto layout = make_vbox<0, 0>(this);
    layout->addWidget(d->tableView);
    layout->addLayout(actionButtonsLayout);

    auto add_new_variable = [this] ()
    {
        d->isModifying = true;

        QList<QStandardItem *> items;
        for (int col = 0; col < d->model->columnCount(); col++)
            items.push_back(new QStandardItem);
        assert(items.size() == 3);
        items[0]->setText("new_var");
        d->model->appendRow(items);
        for (int col = 0; col < 2; col++)
            d->tableView->resizeColumnToContents(col);
        auto newIndex = d->model->index(d->model->rowCount() - 1, 0);
        d->tableView->setCurrentIndex(newIndex);
        d->tableView->selectionModel()->select(
            newIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
        d->tableView->edit(newIndex);

        emit variabledAdded(items[0]->text(), {});

        d->isModifying = false;
    };

    auto delete_selected_variable = [this] ()
    {
        d->isModifying = true;

        auto sm = d->tableView->selectionModel();

        qDebug() << __PRETTY_FUNCTION__ << "selectedIndexes" << sm->selectedIndexes();

        for (const auto &idx: sm->selectedIndexes())
        {
            auto name = d->model->item(idx.row(), 0)->text();
            d->model->removeRow(idx.row());
            emit variableDeleted(name);
        }

        d->isModifying = false;
    };

    // add/delete button clicks
    connect(d->pb_addVariable, &QPushButton::clicked,
            this, add_new_variable);

    connect(d->pb_delVariable, &QPushButton::clicked,
            this, delete_selected_variable);

    // react to model change notifications (currently only variable value
    // changes are handled)
    // Note: this assumes that only a single cell is edited.
    auto on_model_data_changed = [this] (
        const QModelIndex &topLeft,
        const QModelIndex &/*bottomRight*/,
        const QVector<int> &/*roles*/ = QVector<int>())
    {
        //qDebug() << __PRETTY_FUNCTION__ << topLeft << bottomRight << roles << d->isModifying;

        if (d->isModifying)
            return;

        auto row = topLeft.row();

        auto varInfo = get_variable_model_info(*d->model, row);

        if (varInfo.name != varInfo.originalName)
        {
            d->modelSymtab.remove(varInfo.originalName);
            emit variableDeleted(varInfo.originalName);
            emit variabledAdded(varInfo.name, varInfo.var);
        }
        else
        {
            if (d->modelSymtab.value(varInfo.name).value != varInfo.var.value)
                emit variableValueChanged(varInfo.name, varInfo.var);

            if (d->modelSymtab.value(varInfo.name) != varInfo.var)
                emit variableModified(varInfo.name, varInfo.var);
        }

        // Update the symtab representation of the model to reflect the change.
        d->modelSymtab[varInfo.name] = varInfo.var;
    };

    connect(d->model.get(), &QAbstractItemModel::dataChanged,
            this, on_model_data_changed);
}

VariableEditorWidget::~VariableEditorWidget()
{
}

void VariableEditorWidget::setVariables(const vme_script::SymbolTable &symtab)
{
    d->isModifying = true;

    populate_model(*d->model, symtab);
    d->modelSymtab = symtab;

    if (d->tableView->model() != d->model.get())
    {
        if (auto sm = d->tableView->selectionModel())
            sm->deleteLater();

        d->tableView->setModel(d->model.get());
        d->tableView->setTextElideMode(Qt::ElideNone);
        d->tableView->setWordWrap(true);

        // This has to happen after every call to setModel() because a new
        // QItemSelectionModel will have been created internally by the view.
        connect(d->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this] (const QItemSelection &selected, const QItemSelection &/*deselected*/)
        {
            qDebug() << __PRETTY_FUNCTION__;
            if (!selected.isEmpty())
            {
                auto index = selected.indexes().first();
                auto name = d->model->item(index.row(), 0)->text();
                d->pb_delVariable->setEnabled(!vme_script::is_system_variable_name(name));
            }
            else
                d->pb_delVariable->setEnabled(false);
        });
    }

    for (int col = 0; col < 2; col++)
        d->tableView->resizeColumnToContents(col);

    d->isModifying = false;
}

vme_script::SymbolTable VariableEditorWidget::getVariables() const
{
    vme_script::SymbolTable symtab;
    save_to_symboltable(*d->model, symtab);
    return symtab;
}

void VariableEditorWidget::setVariableValue(const QString &varName, const QString &varValue)
{
    d->isModifying = true;

    set_variable_value(*d->model, varName, varValue);

    d->isModifying = false;
}

void VariableEditorWidget::setHideInternalVariables(bool hide)
{
    QString varName;
    vme_script::Variable var;

    for (int row = 0; row < d->model->rowCount(); row++)
    {
        auto varInfo = get_variable_model_info(*d->model, row);

        if (vme_script::is_internal_variable_name(varInfo.name))
            d->tableView->setRowHidden(row, hide);
    }
}
