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
        model.setItem(row, 0, sti.release());

        sti = make_item(var.value, isSysVar);
        model.setItem(row, 1, sti.release());

        sti = make_item(var.comment, isSysVar);
        model.setItem(row, 2, sti.release());
    }
}

void save_to_symboltable(const QStandardItemModel &model, vme_script::SymbolTable &symtab)
{
    for (int row = 0; row < model.rowCount(); row++)
    {
        assert(model.columnCount() == 3);
        auto name = model.item(row, 0)->text();
        auto value = model.item(row, 1)->text();
        auto comment = model.item(row, 2)->text();

        vme_script::Variable var;
        var.value = value;
        var.comment = comment;

        symtab[name] = var;
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

        QValidator::State validate(QString &input, int &pos) const override
        {
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
    vme_script::SymbolTable symtab;

    QPushButton *pb_addVariable,
                *pb_delVariable;
};

// FIXME: VariableEditorWidget: by pressing ESC or clicking anywhere in the
// table duplicate variables can still be created.

VariableEditorWidget::VariableEditorWidget(
    QWidget *parent)
: QWidget(parent)
, d(std::make_unique<Private>())
{
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
        for (int col = 0; col < 2; col++)
            d->tableView->resizeColumnToContents(col);
        auto newIndex = d->model->index(d->model->rowCount() - 1, 0);
        d->tableView->setCurrentIndex(newIndex);
        d->tableView->selectionModel()->select(
            newIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
        d->tableView->edit(newIndex);
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

VariableEditorWidget::~VariableEditorWidget()
{
}

void VariableEditorWidget::setVariables(const vme_script::SymbolTable &symtab)
{
    populate_model(*d->model, symtab);
    if (auto sm = d->tableView->selectionModel())
        sm->deleteLater();
    d->tableView->setModel(d->model.get());
    d->tableView->setTextElideMode(Qt::ElideNone);
    d->tableView->setWordWrap(true);
    for (int col = 0; col < 2; col++)
        d->tableView->resizeColumnToContents(col);

    // This has to happen after every call to setModel() because a new
    // QItemSelectionModel will have been created internally by the view.
    connect(d->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] (const QItemSelection &selected, const QItemSelection &deselected)
    {
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

vme_script::SymbolTable VariableEditorWidget::getVariables() const
{
    vme_script::SymbolTable symtab;
    save_to_symboltable(*d->model, symtab);
    return symtab;
}

void VariableEditorWidget::setVariableValue(const QString &varName, const QString &varValue)
{
    set_variable_value(*d->model, varName, varValue);
}
