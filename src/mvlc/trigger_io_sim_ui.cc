#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QMenu>

#include "mvlc/trigger_io_sim_ui_p.h"
#include "qt_util.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

// 
// BaseModel
//
QStringList BaseModel::pinPathList(const PinAddress &pa) const
{
    return pin_path_list(triggerIO(), pa);
}

QString BaseModel::pinPath(const PinAddress &pa) const
{
    return pin_path(triggerIO(), pa);
}

QString BaseModel::pinName(const PinAddress &pa) const
{
    return pin_name(triggerIO(), pa);
}

QString BaseModel::pinUserName(const PinAddress &pa) const
{
    return pin_user_name(triggerIO(), pa);
}

namespace
{

class TraceItem: public QStandardItem
{
    public:
        TraceItem(const PinAddress &pa = {})
            : QStandardItem()
        {
            setData(QVariant::fromValue(pa), PinRole);
            assert(pa == pinAddress());
        }

        PinAddress pinAddress() const
        {
            assert(data(PinRole).canConvert<PinAddress>());
            return data(PinRole).value<PinAddress>();
        }

        QVariant data(int role = Qt::UserRole + 1) const override
        {
            QVariant result;

            if (role == Qt::DisplayRole)
            {
                const auto pa = pinAddress();

                if (auto m = qobject_cast<TraceTreeModel *>(model()))
                {
                    if (column() == ColUnit)
                        result = m->pinName(pa);
                    else if (column() == ColName)
                        result = m->pinUserName(pa);
                }
                else if (auto m = qobject_cast<TraceTableModel *>(model()))
                {
                    if (column() == ColUnit)
                        result = m->pinPath(pa);
                    else if (column() == ColName)
                        result = m->pinUserName(pa);
                }
            }

            if (!result.isValid())
                result = QStandardItem::data(role);

            return result;
        }

        QStandardItem *clone() const override
        {
            auto ret = new TraceItem;
            *ret = *this; // copies the items data values
            assert(ret->pinAddress() == pinAddress());
            return ret;
        }
};

QStandardItem *make_non_trace_item(const QString &name = {})
{
    auto item = new QStandardItem(name);
    item->setEditable(false);
    item->setDragEnabled(false);
    item->setDropEnabled(false);
    return item;
}

TraceItem *make_trace_item(const PinAddress &pa)
{
    auto item = new TraceItem(pa);
    item->setEditable(false);
    item->setDragEnabled(true);
    item->setDropEnabled(false);
    return item;
};

QList<QStandardItem *> make_trace_row(const PinAddress &pa)
{
    auto unitItem = make_trace_item(pa);
    auto nameItem = make_trace_item(pa);

    return { unitItem, nameItem };
};

} // end anon namespace

std::unique_ptr<TraceTreeModel> make_trace_tree_model()
{
    auto make_lut_item = [] (UnitAddress unit, bool hasStrobe)
        -> QStandardItem *
    {
        auto lutRoot = make_non_trace_item(QSL("LUT%1").arg(unit[1]));

        for (auto i=0; i<LUT::InputBits; ++i)
        {
            unit[2] = i;
            lutRoot->appendRow(make_trace_row({ unit, PinPosition::Input }));
        }

        if (hasStrobe)
        {
            unit[2] = LUT::StrobeGGInput;
            lutRoot->appendRow(make_trace_row({ unit, PinPosition::Input }));
        }

        for (auto i=0; i<LUT::OutputBits; ++i)
        {
            unit[2] = i;
            lutRoot->appendRow(make_trace_row({ unit, PinPosition::Output }));
        }

        if (hasStrobe)
        {
            unit[2] = LUT::StrobeGGOutput;
            lutRoot->appendRow(make_trace_row({ unit, PinPosition::Output }));
        }

        return lutRoot;
    };

    auto model = std::make_unique<TraceTreeModel>();
    auto root = model->invisibleRootItem();

    // Sampled Traces
    auto samplesRoot = make_non_trace_item("sampled");
    root->appendRow({ samplesRoot, make_non_trace_item() });

    for (auto pinAddress: trace_index_to_pin_list())
        samplesRoot->appendRow(make_trace_row(pinAddress));

    // L0
    auto l0Root = make_non_trace_item("L0");
    root->appendRow(l0Root);

    for (auto i=0u; i<TimerCount; ++i)
    {
        UnitAddress unit = { 0, i, 0 };
        l0Root->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    {
        UnitAddress unit = { 0, Level0::SysClockOffset, 0 };
        l0Root->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::NIM_IO_Offset, 0 };
        l0Root->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    for (auto i=0u; i<Level0::IRQ_Inputs_Count; ++i)
    {
        UnitAddress unit = { 0, i+Level0::IRQ_Inputs_Offset, 0 };
        l0Root->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    // L1
    auto l1Root = make_non_trace_item("L1");
    root->appendRow(l1Root);

    for (auto i=0u; i<Level1::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item({ 1, i, 0 }, false);
        l1Root->appendRow(lutRoot);
    }

    // L2
    auto l2Root = make_non_trace_item("L2");
    root->appendRow(l2Root);

    for (auto i=0u; i<Level2::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item({ 2, i, 0 }, true);
        l2Root->appendRow(lutRoot);
    }

    // L3 internal side
    auto l3InRoot = make_non_trace_item("L3in");
    root->appendRow(l3InRoot);

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 3, i+Level3::NIM_IO_Unit_Offset, 0 };
        l3InRoot->appendRow(make_trace_row({ unit, PinPosition::Input }));
    }

    for (auto i=0u; i<ECL_OUT_Count; ++i)
    {
        UnitAddress unit = { 3, i+Level3::ECL_Unit_Offset, 0 };
        l3InRoot->appendRow(make_trace_row({ unit, PinPosition::Input }));
    }

    // L3 output side
    auto l3OutRoot = make_non_trace_item("L3out");
    root->appendRow(l3OutRoot);

    for (auto i=0u; i<NIM_IO_Count; ++i)
    {
        UnitAddress unit = { 3, i+Level3::NIM_IO_Unit_Offset, 0 };
        l3OutRoot->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    for (auto i=0u; i<ECL_OUT_Count; ++i)
    {
        UnitAddress unit = { 3, i+Level3::ECL_Unit_Offset, 0 };
        l3OutRoot->appendRow(make_trace_row({ unit, PinPosition::Output }));
    }

    // Finalize
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");

    return model;
}

std::unique_ptr<TraceTableModel> make_trace_table_model()
{
    auto model = std::make_unique<TraceTableModel>();
    model->setColumnCount(2);
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");
    return model;
}

class TraceTreeView: public QTreeView
{
    public:
        TraceTreeView(QWidget *parent = nullptr)
            : QTreeView(parent)
        {
            setExpandsOnDoubleClick(true);
            setDragEnabled(true);
        }
};

class TraceTableView: public QTableView
{
    public:
        TraceTableView(QWidget *parent = nullptr)
            : QTableView(parent)
        {
            //setSelectionMode(QAbstractItemView::ContiguousSelection);
            setSelectionBehavior(QAbstractItemView::SelectRows);
            setDefaultDropAction(Qt::MoveAction); // internal DnD
            setDragDropMode(QAbstractItemView::DragDrop); // external DnD
            setDragDropOverwriteMode(false);
            setDragEnabled(true);
            verticalHeader()->hide();
            horizontalHeader()->setStretchLastSection(true);
        }
};

struct TraceSelectWidget::Private
{
    TraceSelectWidget *q;
    std::unique_ptr<TraceTreeModel> treeModel;
    std::unique_ptr<TraceTableModel> tableModel;
    TraceTreeView *treeView;
    TraceTableView *tableView;

    void removeSelectedTraces()
    {
        auto selectionModel = this->tableView->selectionModel();

        std::vector<int> rows;
        for (const auto &idx: selectionModel->selectedRows())
            rows.push_back(idx.row());

        // Sort in descending order
        std::sort(std::begin(rows), std::end(rows), std::greater<int>());

        for (int row: rows)
        {
            auto rowItems = this->tableModel->takeRow(row);
            qDeleteAll(rowItems);
        }

        qDebug() << __PRETTY_FUNCTION__ << "emitting selectionChanged()";
        emit q->selectionChanged(q->getSelection());
    }
};

TraceSelectWidget::TraceSelectWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle("TraceSelectWidget");
    d->q = this;
    d->treeModel = make_trace_tree_model();
    d->tableModel = make_trace_table_model();
    d->tableModel->setItemPrototype(new TraceItem);

    d->treeView = new TraceTreeView;
    d->treeView->setModel(d->treeModel.get());

    d->tableView = new TraceTableView;
    d->tableView->setModel(d->tableModel.get());
    d->tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    auto widgetLayout = make_hbox(this);
    widgetLayout->addWidget(d->treeView);
    widgetLayout->addWidget(d->tableView);

    connect(
        d->treeView, &QAbstractItemView::clicked,
        [this] (const QModelIndex &index)
        {
            if (auto item = d->treeModel->itemFromIndex(index))
            {
                qDebug() << "tree clicked, item =" << item
                    << ", row =" << index.row()
                    << ", col =" << index.column()
                    << ", data =" << item->data()
                    ;

                if (item->data().canConvert<PinAddress>())
                    qDebug() << item->data().value<PinAddress>();
            }
        });

    connect(
        d->tableView, &QAbstractItemView::clicked,
        [this] (const QModelIndex &index)
        {
            if (auto item = d->tableModel->itemFromIndex(index))
            {
                qDebug() << "table clicked, item =" << item
                    << ", row =" << index.row()
                    << ", col =" << index.column()
                    << ", data =" << item->data()
                    ;

                if (item->data().canConvert<PinAddress>())
                    qDebug() << item->data().value<PinAddress>();
            }
        });

    // rowsInserted() is emitted when dropping external data and when doing and
    // internal drag-move. It's a better fit than layoutChanged() which is
    // emitted twice on drag-move.
    connect(
        d->tableModel.get(), &QAbstractItemModel::rowsInserted,
        this, [this] ()
        {

            // Note: resizing and the selectionChanged() signal emission is
            // done from the event loop because at the point rowsInserted()
            // is emitted and the slot is called for some reason the
            // internal update of the table model is not completely done
            // yet: rowCount() returns the udpated value but itme(row)
            // still returns nullptr. Using the event loop hack here seems
            // to fix the issue.
            QTimer::singleShot(0, this, [this] () {
                d->tableView->resizeColumnsToContents();
                d->tableView->resizeRowsToContents();
                qDebug() << __PRETTY_FUNCTION__ << "emitting selectionChanged()";
                emit selectionChanged(getSelection());
            });
        });

    // Table context menu
    connect(
        d->tableView, &QWidget::customContextMenuRequested,
        this, [this] (const QPoint &pos)
        {
            QMenu menu;

            auto selectionModel = d->tableView->selectionModel();

            if (!selectionModel->selectedRows().isEmpty())
            {
                menu.addAction(QIcon::fromTheme("edit-delete"), QSL("Remove selected"),
                               [this] () { d->removeSelectedTraces(); });
            }

            if (!menu.isEmpty())
                menu.exec(d->tableView->mapToGlobal(pos));
        });
}

TraceSelectWidget::~TraceSelectWidget()
{
}

void TraceSelectWidget::setTriggerIO(const TriggerIO &trigIO)
{
    d->treeModel->setTriggerIO(trigIO);
    d->tableModel->setTriggerIO(trigIO);
}

void TraceSelectWidget::setSelection(const QVector<PinAddress> &selection)
{
    // FIXME: might have to block signals or set a flag to avoid emitting
    // seletionChanged() during this operation.
    d->tableModel->removeRows(0, d->tableModel->rowCount());

    for (const auto &pa: selection)
        d->tableModel->appendRow(make_trace_row(pa));
}

QVector<PinAddress> TraceSelectWidget::getSelection() const
{
    QVector<PinAddress> result;

    for (int row = 0; row < d->tableModel->rowCount(); ++row)
    {
        assert(d->tableModel->item(row));
        if (auto item = d->tableModel->item(row))
        {
            assert(item->data(PinRole).canConvert<PinAddress>());
            if (item->data(PinRole).canConvert<PinAddress>())
                result.push_back(item->data(PinRole).value<PinAddress>());
        }
    }

    return result;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec

QDataStream &operator<<(QDataStream &out,
                        const mesytec::mvme_mvlc::trigger_io::PinAddress &pa)
{
    for (unsigned val: pa.unit)
        out << val;
    out << static_cast<unsigned>(pa.pos);
    return out;
}

QDataStream &operator>>(QDataStream &in,
                        mesytec::mvme_mvlc::trigger_io::PinAddress &pa)
{
    for (size_t i=0; i<pa.unit.size(); ++i)
        in >> pa.unit[i];
    unsigned pos;
    in >> pos;
    pa.pos = static_cast<mesytec::mvme_mvlc::trigger_io::PinPosition>(pos);
    return in;
}

QDebug operator<<(QDebug dbg, const mesytec::mvme_mvlc::trigger_io::PinAddress &pa)
{
    using namespace mesytec::mvme_mvlc::trigger_io;

    dbg.nospace() << "PinAddress("
        << "ua[0]=" << pa.unit[0]
        << ", ua[1]=" << pa.unit[1]
        << ", ua[2]=" << pa.unit[2]
        << ", pos=" << (pa.pos == PinPosition::Input ? "in" : "out")
        << ")";
    return dbg.maybeSpace();
}
