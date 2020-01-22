#include "vme_config_ui_variable_editor.h"

#include <boost/range/adaptor/indexed.hpp>
#include <QHeaderView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>

#include <QDebug>

#include "qt_util.h"

using boost::adaptors::indexed;

namespace
{

void populate_model(QStandardItemModel &model, const vme_script::SymbolTable &symtab)
{
    model.clear();
    model.setColumnCount(3);
    model.setHorizontalHeaderLabels({"Name", "Value", "Comment"});

    model.setRowCount(symtab.size());

    auto symbolNames = symtab.symbolNames();
    std::sort(symbolNames.begin(), symbolNames.end());

    for (const auto &nameAndIndex: symbolNames | indexed(0))
    {
        auto row = nameAndIndex.index();
        const auto &name = nameAndIndex.value();
        const auto &var = symtab[name];

        auto sti = std::make_unique<QStandardItem>(name);
        model.setItem(row, 0, sti.release());

        sti = std::make_unique<QStandardItem>(var.value);
        model.setItem(row, 1, sti.release());

        sti = std::make_unique<QStandardItem>(var.comment);
        model.setItem(row, 2, sti.release());
    }
}

void save_to_symboltable(const QStandardItemModel &model, vme_script::SymbolTable &symtab)
{
    assert(model.columnCount() == 3);

    for (int row = 0; row < model.rowCount(); row++)
    {
        auto name = model.item(row, 0)->text();
        auto value = model.item(row, 1)->text();
        auto comment = model.item(row, 2)->text();

        vme_script::Variable var;
        var.value = value;
        var.comment = comment;

        symtab[name] = var;
    }
}

} // end anon namespace

struct SymbolTableEditorWidget::Private
{
    QTableView *tableView;
    std::unique_ptr<QStandardItemModel> model;
    vme_script::SymbolTable symtab;

    QPushButton *pb_addVariable,
                *pb_delVariable;
};

SymbolTableEditorWidget::SymbolTableEditorWidget(
    QWidget *parent)
: QWidget(parent)
, d(std::make_unique<Private>())
{
    d->model = std::make_unique<QStandardItemModel>();
    d->tableView = new QTableView(this);
    d->tableView->verticalHeader()->hide();
    d->tableView->setSelectionMode(QAbstractItemView::SingleSelection);

    d->pb_addVariable = new QPushButton(QIcon(":/list_add.png"), "Add new variable");
    d->pb_delVariable = new QPushButton(QIcon(":/list_remove.png"), "Delete selected variable");
    d->pb_delVariable->setEnabled(false);

    auto actionButtonsLayout = make_hbox();
    actionButtonsLayout->addWidget(d->pb_addVariable);
    actionButtonsLayout->addWidget(d->pb_delVariable);
    actionButtonsLayout->addStretch(1);

    auto layout = make_vbox(this);
    layout->addWidget(d->tableView);
    layout->addLayout(actionButtonsLayout);

    auto add_new_variable = [this] ()
    {
        QList<QStandardItem *> items;
        for (int col = 0; col < d->model->columnCount(); col++)
            items.push_back(new QStandardItem);
        assert(items.size() == 3);
        items[0]->setText("new_var");
        d->model->appendRow(items);
        d->tableView->resizeColumnsToContents();
    };

    auto delete_selected_variable = [this] ()
    {
        auto sm = d->tableView->selectionModel();

        qDebug() << __PRETTY_FUNCTION__ << "selectedIndexes" << sm->selectedIndexes();

        for (const auto &idx: sm->selectedIndexes())
            d->model->removeRow(idx.row());
    };

    connect(d->pb_addVariable, &QPushButton::clicked,
            this, add_new_variable);

    connect(d->pb_delVariable, &QPushButton::clicked,
            this, delete_selected_variable);
}

SymbolTableEditorWidget::~SymbolTableEditorWidget()
{
}

void SymbolTableEditorWidget::setSymbolTable(const vme_script::SymbolTable &symtab)
{
    populate_model(*d->model, symtab);
    if (auto sm = d->tableView->selectionModel())
        sm->deleteLater();
    d->tableView->setModel(d->model.get());
    d->tableView->resizeColumnsToContents();

    // This has to happen after every call to setModel() because a new
    // QItemSelectionModel will have been created internally by the view.
    connect(d->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] (const QItemSelection &selected, const QItemSelection &deselected)
    {
        d->pb_delVariable->setEnabled(!selected.isEmpty());
    });
}

vme_script::SymbolTable SymbolTableEditorWidget::getSymbolTable() const
{
    vme_script::SymbolTable symtab;
    save_to_symboltable(*d->model, symtab);
    return symtab;
}
