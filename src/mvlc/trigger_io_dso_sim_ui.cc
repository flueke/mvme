#include "mvlc/trigger_io_dso_sim_ui.h"
#include "mvlc/trigger_io_dso_sim_ui_p.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QMenu>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QtConcurrent>
#include <QTextBrowser>
#include <QTreeView>

#include <chrono>
#include <exception>
#include <qnamespace.h>
#include <stdexcept>
#include <thread>
#include <yaml-cpp/yaml.h>

#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/trigger_io_dso_plot_widget.h"
#include "mvlc/trigger_io_sim.h"
#include "util/qt_font.h"
#include "util/qt_model_view_util.h"
#include "util/qledindicator.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

using namespace mesytec::mvme;

//
// Trace and Trigger Selection
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
    auto samplesRoot = make_non_trace_item("Samples+Triggers");
    model->samplesRoot = samplesRoot;
    root->appendRow({ samplesRoot, make_non_trace_item() });

    for (auto pinAddress: trace_index_to_pin_list())
    {
        auto row = make_trace_row(pinAddress);
        row[0]->setFlags(row[0]->flags() | Qt::ItemIsUserCheckable);
        row[0]->setCheckState(Qt::Unchecked);
        samplesRoot->appendRow(row);
    }

#define ENABLE_SIMULATED_TRACES 1

#if ENABLE_SIMULATED_TRACES
    // L0 NIMs and IRQs
    auto l0Root = make_non_trace_item("L0");
    root->appendRow(l0Root);

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
#endif

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
    CombinedTriggers triggerBits;

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

        //qDebug() << __PRETTY_FUNCTION__ << "emitting selectionChanged()";
        emit q->selectionChanged(q->getSelection());
    }

#if 0
    void debugClickPinAddress(const PinAddress &pa)
    {
        qDebug() << "click on" << pa;

        if (pa.pos == PinPosition::Input)
        {
            qDebug() << "connection source address: " <<
                get_connection_unit_address(
                    treeModel->triggerIO(), pa.unit);
        }
    }
#endif
};

TraceSelectWidget::TraceSelectWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    qRegisterMetaType<mesytec::mvme_mvlc::trigger_io::PinAddress>();
    qRegisterMetaTypeStreamOperators<mesytec::mvme_mvlc::trigger_io::PinAddress>(
        "mesytec::mvme_mvlc::trigger_io::PinAddress");

    setWindowTitle("TraceSelectWidget");
    d->q = this;
    d->treeModel = make_trace_tree_model();
    d->tableModel = make_trace_table_model();
    d->tableModel->setItemPrototype(new TraceItem);

    d->treeView = new TraceTreeView;
    d->treeView->setModel(d->treeModel.get());
    d->treeView->resizeColumnToContents(0);
    d->treeView->resizeColumnToContents(1);

    d->tableView = new TraceTableView;
    d->tableView->setModel(d->tableModel.get());
    d->tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    auto splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(d->treeView);
    splitter->addWidget(d->tableView);

    auto widgetLayout = make_hbox(this);
    widgetLayout->addWidget(splitter);

    connect(
        d->treeView, &QAbstractItemView::clicked,
        [this] (const QModelIndex &index)
        {
            if (auto item = d->treeModel->itemFromIndex(index))
            {
                if (item->data().canConvert<PinAddress>())
                {
                    //d->debugClickPinAddress(item->data().value<PinAddress>());
                    emit debugTraceClicked(item->data().value<PinAddress>());
                }
            }
        });

    // Detect checkstate changes on the items contributing to the trigger bits.
    connect(
        d->treeModel.get(), &QStandardItemModel::itemChanged,
        [this] (const QStandardItem *item)
        {
            if (item->parent() == d->treeModel->samplesRoot)
            {
                int triggerIndex = item->row();
                assert(triggerIndex < static_cast<int>(d->triggerBits.size()));
                bool isChecked = item->checkState() == Qt::Checked;

                if (d->triggerBits.test(triggerIndex) != isChecked)
                {
                    d->triggerBits.set(triggerIndex, isChecked);
                    //qDebug() << __PRETTY_FUNCTION__ << "emit triggersChanged";
                    emit triggersChanged(d->triggerBits);
                }
            }
        });


    connect(
        d->tableView, &QAbstractItemView::clicked,
        [this] (const QModelIndex &index)
        {
            if (auto item = d->tableModel->itemFromIndex(index))
            {
                if (item->data().canConvert<PinAddress>())
                {
                    //d->debugClickPinAddress(item->data().value<PinAddress>());
                    emit debugTraceClicked(item->data().value<PinAddress>());
                }
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
            // yet: rowCount() returns the udpated value but item(row)
            // still returns nullptr. Using the event loop hack here seems
            // to fix the issue.
            QTimer::singleShot(0, this, [this] () {
                // Remove the checkboxes that are present when sample items are
                // dropped here.
                for (int row=0; row<d->tableModel->rowCount(); ++row)
                {
                    if (auto item = d->tableModel->item(row, 0))
                    {
                        item->setFlags(item->flags() & (~Qt::ItemIsUserCheckable));
                        item->setData(QVariant(), Qt::CheckStateRole);
                    }
                }
                d->tableView->resizeColumnToContents(0);
                d->tableView->resizeRowsToContents();
                //qDebug() << __PRETTY_FUNCTION__ << "emitting selectionChanged()";
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
    auto expansionState = make_expansion_state(d->treeView, d->treeModel.get());
    d->treeModel->setTriggerIO(trigIO);
    set_expansion_state(d->treeView, expansionState);
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

void TraceSelectWidget::setTriggers(const CombinedTriggers &triggers)
{
    assert(d->treeModel->samplesRoot);
    assert(static_cast<size_t>(d->treeModel->samplesRoot->rowCount()) == triggers.size());

    for (size_t i=0; i<triggers.size(); ++i)
    {
        auto item = d->treeModel->samplesRoot->child(i, 0);
        item->setCheckState(triggers.test(i) ? Qt::Checked : Qt::Unchecked);
    }
}

CombinedTriggers TraceSelectWidget::getTriggers() const
{
    return d->triggerBits;
}

TreeViewExpansionState TraceSelectWidget::getTreeExpansionState() const
{
    return make_expansion_state(d->treeView, d->treeModel.get());
}

void TraceSelectWidget::setTreeExpansionState(const TreeViewExpansionState &expansionState)
{
    return set_expansion_state(d->treeView, expansionState);
}

//
// DSOControlWidget
//

struct DSOControlWidget::Private
{
    QSpinBox *spin_preTriggerTime,
             *spin_measureDuration,
             *spin_postTriggerTime,
             *spin_interval;

    static const int TriggerCols = 6;
    static const int TriggerRows = 6;

    QWidget *setupWidget;
    QPushButton *pb_start,
                *pb_stop;
    QLedIndicator *triggerLed;
    QLabel *l_lastTriggerTime;

    const QColor ledActiveOffColor1 = QColor(0,28,0);
    const QColor ledActiveOffColor2 = QColor(0,128,0);
    const QColor ledInactiveOffColor1 = QColor(115, 0, 0);
    const QColor ledInactiveOffColor2 = QColor(100, 0, 0);
};

DSOControlWidget::DSOControlWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle("DSOControlWidget");
    d->spin_preTriggerTime = new QSpinBox;
    d->spin_measureDuration = new QSpinBox;
    d->spin_postTriggerTime = new QSpinBox;

    for (auto spin: { d->spin_preTriggerTime, d->spin_measureDuration, d->spin_postTriggerTime })
    {
        spin->setMinimum(0);
        spin->setMaximum(std::numeric_limits<u16>::max());
        spin->setSuffix(" ns");
    }

    d->spin_postTriggerTime->setReadOnly(true);
    {
        auto pal = d->spin_postTriggerTime->palette();
        pal.setColor(QPalette::Base, Qt::lightGray);
        d->spin_postTriggerTime->setPalette(pal);
    }

    d->spin_interval = new QSpinBox;
    d->spin_interval->setMinimum(0);
    d->spin_interval->setMaximum(5000);
    d->spin_interval->setSingleStep(10);
    d->spin_interval->setSpecialValueText("once");
    d->spin_interval->setSuffix(" ms");
    d->spin_interval->setValue(500);

    d->setupWidget = new QWidget;
    auto setupLayout = new QFormLayout(d->setupWidget);
    setupLayout->addRow("Pre Trigger Time", d->spin_preTriggerTime);
    setupLayout->addRow("Measure Duration", d->spin_measureDuration);
    setupLayout->addRow("Post Trigger Time",d->spin_postTriggerTime);
    setupLayout->addRow("Min. Read Interval", d->spin_interval);

    d->pb_start = new QPushButton("Start DSO");
    d->pb_stop = new QPushButton("Stop DSO");
    d->triggerLed = new QLedIndicator;
    d->l_lastTriggerTime = new QLabel("Last Trigger");

    auto controlLayout = make_hbox();
    controlLayout->addWidget(d->pb_start);
    controlLayout->addWidget(d->pb_stop);
    controlLayout->addWidget(d->triggerLed);
    controlLayout->addWidget(d->l_lastTriggerTime);
    controlLayout->setStretch(0, 1);
    controlLayout->setStretch(1, 1);
    controlLayout->setStretch(3, 0);

    auto widgetLayout = make_vbox<4, 4>();
    widgetLayout->addWidget(d->setupWidget);
    widgetLayout->addLayout(controlLayout);

    setLayout(widgetLayout);


    connect(d->pb_start, &QPushButton::clicked,
        this, &DSOControlWidget::startDSO);

    connect(d->pb_stop, &QPushButton::clicked,
        this, &DSOControlWidget::stopDSO);

    auto update_post_trigger_time = [this]
    {
        auto postTime = std::abs(d->spin_measureDuration->value() - d->spin_preTriggerTime->value());
        d->spin_postTriggerTime->setValue(postTime);
    };

    connect(d->spin_preTriggerTime, qOverload<int>(&QSpinBox::valueChanged),
        this, [=]
    {
        d->spin_measureDuration->setMinimum(d->spin_preTriggerTime->value());
        update_post_trigger_time();
        emit restartDSO();
    });

    connect(d->spin_measureDuration, qOverload<int>(&QSpinBox::valueChanged),
        this, [=]
    {
        update_post_trigger_time();
        emit restartDSO();
    });

    d->spin_preTriggerTime->setValue(500);
    d->spin_measureDuration->setValue(1000);

    setDSOActive(false);
}

DSOControlWidget::~DSOControlWidget()
{
}

void DSOControlWidget::setDSOActive(bool active)
{
    d->pb_start->setEnabled(!active);
    d->pb_stop->setEnabled(active);

    d->triggerLed->setOffColor1(active ? d->ledActiveOffColor1 : d->ledInactiveOffColor1);
    d->triggerLed->setOffColor2(active ? d->ledActiveOffColor2 : d->ledInactiveOffColor2);
    d->triggerLed->setChecked(false);
}

void DSOControlWidget::dsoTriggered()
{
    if (!d->triggerLed->isChecked())
    {
        d->triggerLed->setChecked(true);
        QTimer::singleShot(100, Qt::PreciseTimer, d->triggerLed, [this] () {
            d->triggerLed->setChecked(false);
        });
    }
}

unsigned DSOControlWidget::getPreTriggerTime()
{
    return d->spin_preTriggerTime->value();
}

unsigned DSOControlWidget::getPostTriggerTime()
{
    return d->spin_postTriggerTime->value();
}

std::chrono::milliseconds DSOControlWidget::getInterval() const
{
    return std::chrono::milliseconds(d->spin_interval->value());
}

// fill gui with settings values
void DSOControlWidget::setDSOSettings(
    unsigned preTriggerTime,
    unsigned postTriggerTime,
    const std::chrono::milliseconds &interval)
{
    d->spin_preTriggerTime->setValue(preTriggerTime);
    auto duration = postTriggerTime + preTriggerTime;
    d->spin_measureDuration->setValue(duration);
    d->spin_postTriggerTime->setValue(postTriggerTime);
    d->spin_interval->setValue(interval.count());
}

void DSOControlWidget::setLastTriggerTime(const QTime &t)
{
    auto deltaSeconds = t.secsTo(QTime::currentTime());
    auto tStr = t.isValid() ? QSL("%1s ago").arg(deltaSeconds) : QSL("N/A");
    d->l_lastTriggerTime->setText(QSL("Last Trigger: %1").arg(tStr));
}

namespace
{

struct DSOSimGuiState
{
    DSOSetup dsoSetup;
    std::chrono::milliseconds dsoInterval;
    QVector<PinAddress> traceSelection;
    std::vector<std::vector<int>> traceTreeExpansionState;
};

void to_yaml(YAML::Emitter &out, const DSOSetup &dsoSetup)
{
    out << YAML::BeginMap;

    out << YAML::Key << "preTriggerTime"
        << YAML::Value << dsoSetup.preTriggerTime;

    out << YAML::Key << "postTriggerTime"
        << YAML::Value << dsoSetup.postTriggerTime;

    out << YAML::Key << "nimTriggers"
        << YAML::Value << dsoSetup.nimTriggers.to_ulong();

    out << YAML::Key << "irqTriggers"
        << YAML::Value << dsoSetup.irqTriggers.to_ulong();

    out << YAML::Key << "utilTriggers"
        << YAML::Value << dsoSetup.utilTriggers.to_ulong();

    out << YAML::EndMap;
}

DSOSetup dso_setup_from_yaml(const YAML::Node &node)
{
    DSOSetup result;

    try
    {
        if (node["preTriggerTime"])
            result.preTriggerTime = node["preTriggerTime"].as<u16>();

        if (node["postTriggerTime"])
            result.postTriggerTime = node["postTriggerTime"].as<u16>();

        if (node["nimTriggers"])
            result.nimTriggers = node["nimTriggers"].as<unsigned long>();

        if (node["irqTriggers"])
            result.irqTriggers = node["irqTriggers"].as<unsigned long>();

        if (node["utilTriggers"])
            result.utilTriggers = node["utilTriggers"].as<unsigned long>();

    } catch (const YAML::Exception &)
    {}

    return result;
}

void to_yaml(YAML::Emitter &out, const QVector<PinAddress> &traceSelection)
{
    out << YAML::BeginSeq;

    for (const auto &pa: traceSelection)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq;

        for (const auto &value: pa.unit)
            out << value;

        out << static_cast<int>(pa.pos);

        out << YAML::EndSeq;
    }

    out << YAML::EndSeq;
}

QVector<PinAddress> trace_selection_from_yaml(const YAML::Node &node)
{
    QVector<PinAddress> result;

    for (const auto &yPinAddress: node)
    {
        PinAddress pa;

        for (size_t i=0; i<pa.unit.size(); ++i)
            pa.unit[i] = yPinAddress[i].as<unsigned>();

        pa.pos = static_cast<PinPosition>(yPinAddress[pa.unit.size()].as<int>());

        result.push_back(pa);
    }

    return result;
}

void to_yaml(YAML::Emitter &out, const mvme::TreeViewExpansionState &expansionState)
{
    out << YAML::BeginSeq;

    for (auto &entry: expansionState)
        out << YAML::Flow << entry;

    out << YAML::EndSeq;
}

mvme::TreeViewExpansionState tree_view_expansion_state_from_yaml(const YAML::Node &node)
{
    mvme::TreeViewExpansionState result;

    for (const auto &yEntry: node)
    {
        auto entry = yEntry.as<std::vector<int>>();
        result.emplace_back(entry);
    }

    return result;
}

QString to_yaml(const DSOSimGuiState &guiState)
{
    YAML::Emitter out;

    out << YAML::BeginMap;

    out << YAML::Key << "DSOSetup"
        << YAML::Value;
    to_yaml(out, guiState.dsoSetup);

    out << YAML::Key << "DSOInterval"
        << YAML::Value << guiState.dsoInterval.count();

    out << YAML::Key << "TraceSelection"
        << YAML::Value;
    to_yaml(out, guiState.traceSelection);

    out << YAML::Key << "TraceTreeExpansion"
        << YAML::Value;
    to_yaml(out, guiState.traceTreeExpansionState);

    assert(out.good());

    return QString(out.c_str());
}

DSOSimGuiState dso_sim_gui_state_from_yaml(const QString &yamlString)
{
    DSOSimGuiState result;

    YAML::Node yRoot = YAML::Load(yamlString.toStdString());

    if (!yRoot) return result;

    if (yRoot["DSOSetup"])
        result.dsoSetup = dso_setup_from_yaml(yRoot["DSOSetup"]);

    if (yRoot["DSOInterval"])
        result.dsoInterval = std::chrono::milliseconds(yRoot["DSOInterval"].as<s64>());

    if (yRoot["TraceSelection"])
        result.traceSelection = trace_selection_from_yaml(yRoot["TraceSelection"]);

    if (yRoot["TraceTreeExpansion"])
        result.traceTreeExpansionState = tree_view_expansion_state_from_yaml(yRoot["TraceTreeExpansion"]);

    return result;
}

/* How the DSOSimWidget currently works:
 * - On clicking 'start' in the DSO control widget the DSOSimWidget runs both
 *   the dso sampling and the sim in a separate thread using QtConcurrent run.
 * - The widget uses a QFutureWatcher to keep track of the status of the async
 *   operation.
 * - Once sampling and simulation are done the local result is updated and the
 *   new traces are copied into the plot widget based on the current trace
 *   selection.
 * - If a non-zero interval is set the async operation is restarted via a
 *   QTimer.
 *
 * The code is not focused on performance. A new Sim structure is created and
 * filled on every invocation of run_dso_and_sim. Also traces are copied to the
 * plot widget.
 */

struct DSO_Sim_Result
{
    // Error code from the MVLC io operations or set to std::errc::io_error
    // when an exception is caught.
    std::error_code ec;
    // Copy of the caught exception if any.
    std::exception_ptr ex;
    std::vector<u32> dsoBuffer; // Raw DSO data
    Sim sim;
    bool wasTriggered = false;
    std::chrono::microseconds acquireDuration;
};

// This is a bit ugly: sim.sampledTraces can diverge from what was last
// read into dsoBuffer. It has to as otherwise the whole gui will be populated
// with the sim calculated from a possibly empty dsoBuffer instead of happily
// displaying the previous good state. Handle the whole thing in a better way
// if possible.
DSO_Sim_Result run_dso_and_sim(
    mvlc::MVLC mvlc,
    DSOSetup dsoSetup,
    TriggerIO trigIO,
    SampleTime simMaxTime,
    std::atomic<bool> &cancel)
{
    DSO_Sim_Result result = {};

    // acquire
    try
    {
        // Immediately copy the trigIO config into the result. The widget reuses
        // that copy for its internal state once this function returns.
        result.sim.trigIO = trigIO;

        auto acqStart = std::chrono::steady_clock::now();

        if (auto ec = acquire_dso_sample(mvlc, dsoSetup, result.dsoBuffer, cancel))
        {
            result.ec = ec;
            return result;
        }

        result.acquireDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - acqStart);

        auto sampledTraces = fill_snapshot_from_dso_buffer(result.dsoBuffer);

        if (sampledTraces.empty())
            return result;

        auto traceOverflows = remove_trace_overflow_markers(sampledTraces);
        jitter_correct_dso_snapshot(sampledTraces, dsoSetup);

        result.sim.sampledTraces = sampledTraces;
        result.sim.traceOverflows = std::move(traceOverflows);
        result.wasTriggered = true;
    }
    // uglyness begins
    catch (const std::system_error &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! system_error in run_dso_and_sim: " << e.what()
            << e.code().message().c_str();
        result.ec = e.code();
        result.ex = std::current_exception();
    }
    catch (const std::runtime_error &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! runtime_error in run_dso_and_sim: " << e.what();
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }
    catch (const std::exception &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! std::exception in run_dso_and_sim: " << e.what();
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }
    catch (...)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! unknown exception in run_dso_and_sim!";
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }

    // Exit point to save on simulating if cancelation was requested.
    if (cancel)
        return result;

    // simulate
#if 1
    try
    {
        simulate(result.sim, simMaxTime);

        const auto postTrigger = SampleTime(dsoSetup.postTriggerTime + dsoSetup.preTriggerTime);

        // sampled traces
        pre_extend_traces(result.sim.sampledTraces, result.sim.traceOverflows);
        post_extend_traces_to(result.sim.sampledTraces, postTrigger);

        // simulated traces
        pre_extend_traces(result.sim.l0_traces);
        post_extend_traces_to(result.sim.l0_traces, postTrigger);

        for (auto &lutTraces: result.sim.l1_luts)
        {
            pre_extend_traces(lutTraces);
            post_extend_traces_to(lutTraces, postTrigger);
        }

        for (auto &lutTraces: result.sim.l2_luts)
        {
            pre_extend_traces(lutTraces);
            post_extend_traces_to(lutTraces, postTrigger);
        }

        pre_extend_traces(result.sim.l3_traces);
        post_extend_traces_to(result.sim.l3_traces, postTrigger);
    }
    // uglyness begins once more
    catch (const std::system_error &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! system_error from simulate: " << e.what()
            << e.code().message().c_str();
        result.ec = e.code();
        result.ex = std::current_exception();
    }
    catch (const std::runtime_error &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! runtime_error from simulate: " << e.what();
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }
    catch (const std::exception &e)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! std::exception from simulate: " << e.what();
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }
    catch (...)
    {
        qDebug() << __PRETTY_FUNCTION__ << "!!! unknown exception from simulate!";
        // assign something so the caller can react
        result.ec = make_error_code(std::errc::io_error);
        result.ex = std::current_exception();
    }
#endif

    return result;
}

bool is_trigger_pin(const PinAddress &pa, const DSOSetup &dsoSetup)
{
    auto combinedTriggers = get_combined_triggers(dsoSetup);
    int idx = get_trace_index(pa);

    if (idx < 0)
        return false;

    return combinedTriggers.test(idx);
}

void show_dso_buffer_debug_widget(
    const DSO_Sim_Result &dsoSimResult,
    const DSOSetup &dsoSetup)
{
    QString text;
    QTextStream out(&text);

    {
        auto combinedTriggers = get_combined_triggers(dsoSetup);
        const auto &dsoBuffer = dsoSimResult.dsoBuffer;
        auto sampledTraces = fill_snapshot_from_dso_buffer(dsoBuffer);
        auto jitter = calculate_jitter_value(sampledTraces, dsoSetup).first;

        out << "<html><body><pre>";

        if (dsoSimResult.ec)
            out << "Result: error_code: " << dsoSimResult.ec.message().c_str() << endl;

        if (dsoSimResult.ex)
        {
            out << "Result: exception: ";
            try
            {
                std::rethrow_exception(dsoSimResult.ex);
            }
            catch (const std::system_error &e)
            {
                out << "system_error: " << e.what();
            }
            catch (const std::runtime_error &e)
            {
                out << "runtime_error: " << e.what();
            }
            catch (const std::exception &e)
            {
                out << "std::exception: " << e.what();
            }
            catch (...)
            {
                out << "unhandled exception";
            }

            out << endl << endl;
        }

        out << "DSO setup: preTriggerTime=" << dsoSetup.preTriggerTime
            << ", postTriggerTime=" << dsoSetup.postTriggerTime
            << endl;

        out << "Calculated jitter: " << jitter << endl;

        out << "DSO buffer (size=" << dsoBuffer.size() << "):"  << endl;

        for (size_t i=0; i<dsoBuffer.size(); ++i)
        {
            u32 word = dsoBuffer[i];

            auto ft = mvlc::get_frame_type(word);

            QString line = QSL("%1: ").arg(i, 3, 10, QLatin1Char(' '));

            line += QString("0x%1").arg(word, 8, 16, QLatin1Char('0'));

            // The magic 4 is to skip over the framing and header words, e.g.:
            //   0: 0xf38000fe
            //   1: 0x00000311
            //   2: 0xf58000fc
            //   3: 0x40000000
            // The -1 offset is to skip the 0xc0000000 footer word.

            if (4 <= i && i < dsoBuffer.size() - 1
                && ! (ft == mvlc::frame_headers::StackFrame
                      || ft == mvlc::frame_headers::StackContinuation
                      || ft == mvlc::frame_headers::BlockRead)
               )
            {
                auto entry = extract_dso_entry(word);

                line +=  "    ";
                line += (QSL("addr=%1, time=%2, edge=%3, name=%4")
                        .arg(static_cast<unsigned>(entry.address), 2, 10, QLatin1Char(' '))
                        .arg(entry.time, 5, 10, QLatin1Char(' '))
                        .arg(static_cast<int>(entry.edge))
                        .arg(get_trigger_default_name(entry.address))
                        );

                // Make text for triggering addresses italic
                if (entry.address < combinedTriggers.size()
                    && combinedTriggers.test(entry.address))
                {
                    line = "<i>" + line + "</i>";
                }
            }

            out << line << endl;
        }

        out << "-----" << endl;

        out << "</pre></body></html>";
    }

    {
        auto widget = new QTextBrowser;
        widget->setAttribute(Qt::WA_DeleteOnClose);
        widget->setWindowTitle("MVLC DSO Debug");
        widget->setFont(make_monospace_font());
        add_widget_close_action(widget);
        auto geoSaver = new WidgetGeometrySaver(widget);
        widget->resize(800, 600);
        geoSaver->addAndRestore(widget, QSL("MVLCTriggerIOEditor/DSODebugWidgetGeometry"));

        auto textDoc = new QTextDocument(widget);
        textDoc->setHtml(text);
        widget->setDocument(textDoc);

        widget->show();
    }
}

void show_trace_debug_widget(const Trace &trace, const QString &name)
{
    QString text;
    QTextStream out(&text);

    {
        out << "<html><body><pre>";

        out << "Trace Name: " << name << endl;
        out << "Trace Size: " << trace.size() << endl;

        if (!trace.empty())
        {
            out << "First Time: " << trace.front().time.count() << endl;
            out << "Last Time: " << trace.back().time.count() << endl;
            out << endl;

            for (size_t sampleIndex = 0; sampleIndex < trace.size(); ++sampleIndex)
            {
                if (sampleIndex > 0 && sampleIndex % 4 == 0)
                    out << endl;

                const auto &sample = trace[sampleIndex];
                out << "(" << qSetFieldWidth(8) << sample.time.count() << qSetFieldWidth(0)
                    << ", " << to_string(sample.edge) << ")";

                if (sampleIndex < trace.size() - 1)
                    out << ", ";

            }

            out << endl;
        }


        out << "</pre></body></html>";
    }

    {
        auto widget = new QTextBrowser;
        widget->setAttribute(Qt::WA_DeleteOnClose);
        widget->setWindowTitle("MVLC DSO Trace Debug Info");
        widget->setFont(make_monospace_font());
        add_widget_close_action(widget);
        auto geoSaver = new WidgetGeometrySaver(widget);
        widget->resize(800, 600);
        geoSaver->addAndRestore(widget, QSL("MVLCTriggerIOEditor/TraceDebugWidgetGeometry"));

        auto textDoc = new QTextDocument(widget);
        textDoc->setHtml(text);
        widget->setDocument(textDoc);

        widget->show();
    }
}

template<typename Lock> void safe_unlock(Lock &lock)
{
    try
    {
        lock.unlock();
    } catch (const std::system_error &) {}
}

} // end anon namespace

struct DSOSimWidget::Private
{
    struct Stats
    {
        size_t sampleCount;
        QTime lastSampleTime;
        size_t errorCount;
    };

    enum class DebugAction
    {
        None,
        Next,
    };

    const char *GUIStateFileName = "mvlc_dso_sim_gui_state.yaml";
    size_t GUIStateFileMaxSize = Megabytes(1);

    DSOSimWidget *q;

    VMEScriptConfig *trigIOScript;
    mvlc::MVLC mvlc;
    std::atomic<bool> cancelDSO; // to communicate with the acquire thread
    bool dsoStoppedByUser = true; // true if the "stop" button has been clicked
    DSO_Sim_Result lastResult;
    Stats stats = {};
    DebugAction debugAction;

    QFutureWatcher<DSO_Sim_Result> resultWatcher;

    DSOControlWidget *dsoControlWidget;
    TraceSelectWidget *traceSelectWidget;
    DSOPlotWidget *dsoPlotWidget;
    QLabel *label_status;
    QTimer statusUpdateTimer;

    void onTriggerIOModified()
    {
        auto trigIO = parse_trigger_io_script_text(this->trigIOScript->getScriptContents());
        this->traceSelectWidget->setTriggerIO(trigIO);
        this->lastResult.sim.trigIO = trigIO;

        // Update the names in the plot widget by rebuilding the trace list
        // from the current selection.
        updatePlotTraces();
    }

    DSOSetup buildDSOSetup() const
    {
        DSOSetup dsoSetup;
        dsoSetup.preTriggerTime = dsoControlWidget->getPreTriggerTime();
        dsoSetup.postTriggerTime = dsoControlWidget->getPostTriggerTime();
        auto combinedTriggers = traceSelectWidget->getTriggers();
        set_combined_triggers(dsoSetup, combinedTriggers);
        return dsoSetup;
    }

    void updatePlotTraces()
    {
        auto selection = traceSelectWidget->getSelection();
        auto dsoSetup = buildDSOSetup();

        Snapshot traces;
        QStringList traceNames;
        std::vector<bool> isTriggerTrace; // the evil bool vector :O

        traces.reserve(selection.size());
        traceNames.reserve(selection.size());

        for (const auto &pa: selection)
        {
            if (auto trace = lookup_trace(this->lastResult.sim, pa))
            {
                QString name = QSL("%1 (%2)")
                    .arg(pin_path(this->lastResult.sim.trigIO, pa))
                    .arg(pin_user_name(this->lastResult.sim.trigIO, pa))
                    ;

                bool isTrigger = is_trigger_pin(pa, dsoSetup);

                if (isTrigger)
                    name = QSL("<i>%1</i>").arg(name);

                traces.push_back(*trace); // copy the trace
                traceNames.push_back(name);
                isTriggerTrace.push_back(isTrigger);
            }
        }

        std::reverse(std::begin(traces), std::end(traces));
        std::reverse(std::begin(traceNames), std::end(traceNames));
        std::reverse(std::begin(isTriggerTrace), std::end(isTriggerTrace));

        this->dsoPlotWidget->setXInterval(
            -1.0 * dsoSetup.preTriggerTime, getSimMaxTime().count() - dsoSetup.preTriggerTime);
        // Have to use the masked pre trigger time here, otherwise the triggered
        // rising edge does not align with the plots 0 time point. Reason: the
        // MVLC scope is not accurate enough for the lowest 3-bits. Also see
        // calculate_jitter_value().
        const auto maskedPreTrig = dsoSetup.preTriggerTime & ~0b111;
        this->dsoPlotWidget->setTraces(traces, maskedPreTrig, traceNames);
        this->dsoPlotWidget->setPreTriggerTime(-1.0 * maskedPreTrig);
        this->dsoPlotWidget->setPostTriggerTime(dsoSetup.postTriggerTime);
        this->dsoPlotWidget->setTriggerTraceInfo(isTriggerTrace);
    }

    void startDSO()
    {
        if (this->resultWatcher.isRunning())
            return;

        this->dsoStoppedByUser = false;
        this->cancelDSO = false;
        this->dsoControlWidget->setDSOActive(true);
        this->stats = {};

        runDSO();
        updateStatusInfo();
    }

    void stopDSO()
    {
        this->dsoStoppedByUser = true;
        this->cancelDSO = true;
    }

    void restartDSO()
    {
        if (this->dsoStoppedByUser)
            return;

        this->cancelDSO = true;
    }

    void runDSO()
    {
        if (this->resultWatcher.isRunning())
        {
            qDebug() << "runDSO: returning because DSO is running!";
            return;
        }

        auto future = QtConcurrent::run(
            run_dso_and_sim,
            this->mvlc,
            this->buildDSOSetup(),
            this->lastResult.sim.trigIO,
            getSimMaxTime(),
            std::ref(this->cancelDSO));

        this->resultWatcher.setFuture(future);
    }

    void onDSOSimRunFinished()
    {
        auto result = resultWatcher.result();

        if (!this->cancelDSO)
        {
            if (result.ec)
                ++stats.errorCount;

            if (debugAction == DebugAction::Next)
            {
                debugAction = {};
                show_dso_buffer_debug_widget(result, this->buildDSOSetup());
            }
        }

        if (!this->cancelDSO && result.wasTriggered)
        {
            this->lastResult = result;
            ++stats.sampleCount;
            stats.lastSampleTime = QTime::currentTime();
            dsoControlWidget->dsoTriggered();

            updatePlotTraces();
        }

        auto interval = this->dsoControlWidget->getInterval();

        if (!this->dsoStoppedByUser && interval != std::chrono::milliseconds::zero())
        {
            QTimer::singleShot(interval, q, [this] () {
                this->cancelDSO = false;
                runDSO();
            });
        }
        else
        {
            qDebug() << "onDSOSimRunFinished: stopping DSO, no new timer enqueued";
            this->cancelDSO = true;
            this->dsoControlWidget->setDSOActive(false);
        }

        updateStatusInfo();
    }

    SampleTime getSimMaxTime() const
    {
        auto dsoSetup = this->buildDSOSetup();
#if 0
        // Simulate up to twice the time interval between the pre and post
        // trigger times.
        SampleTime simMaxTime((dsoSetup.postTriggerTime + dsoSetup.preTriggerTime) * 2);
#else
        // Simulate up to the postTriggerTime
        SampleTime simMaxTime((dsoSetup.postTriggerTime + dsoSetup.preTriggerTime));
#endif
        return simMaxTime;
    }

    void saveGUIState()
    {
        QFile outFile(GUIStateFileName);

        if (outFile.open(QIODevice::WriteOnly))
        {
            DSOSimGuiState state;
            state.dsoSetup = buildDSOSetup();
            state.dsoInterval = dsoControlWidget->getInterval();
            state.traceSelection = traceSelectWidget->getSelection();
            state.traceTreeExpansionState = traceSelectWidget->getTreeExpansionState();

            outFile.write(to_yaml(state).toUtf8());
        }
    }

    void loadGUIState()
    {
        QFile inFile(GUIStateFileName);

        if (inFile.open(QIODevice::ReadOnly))
        {
            auto yStr = QString::fromUtf8(inFile.read(GUIStateFileMaxSize));
            auto state = dso_sim_gui_state_from_yaml(yStr);

            this->dsoControlWidget->setDSOSettings(
                state.dsoSetup.preTriggerTime,
                state.dsoSetup.postTriggerTime,
                state.dsoInterval);
            auto combinedTriggers = get_combined_triggers(state.dsoSetup);
            this->traceSelectWidget->setTriggers(combinedTriggers);
            this->traceSelectWidget->setSelection(state.traceSelection);
            this->traceSelectWidget->setTreeExpansionState(state.traceTreeExpansionState);
        }
    }

    void updateStatusInfo()
    {
        QString str = QSL("Status: %1, Triggers: %2, Last Trigger: %3")
            .arg(cancelDSO ? "inactive" : "active")
            .arg(stats.sampleCount)
            .arg(stats.lastSampleTime.toString())
            ;

        if (stats.errorCount)
            str += QSL(", Errors: %1").arg(stats.errorCount);

        label_status->setText(str);

        dsoControlWidget->setLastTriggerTime(stats.lastSampleTime);
    }

    void debugOnTraceClicked(const PinAddress &pa)
    {
        auto &sim = lastResult.sim;

        qDebug() << "click on" << pa;

        if (pa.pos == PinPosition::Input)
        {
            qDebug() << "  connection source address: " <<
                get_connection_unit_address(sim.trigIO, pa.unit);
        }

        qDebug() << "  trace:" << lookup_trace(sim, pa);
    }
};

DSOSimWidget::DSOSimWidget(
    VMEScriptConfig *trigIOScript,
    mvlc::MVLC mvlc,
    QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->trigIOScript = trigIOScript;
    d->mvlc = mvlc;

    d->dsoControlWidget = new DSOControlWidget;
    d->traceSelectWidget = new TraceSelectWidget;
    d->dsoPlotWidget = new DSOPlotWidget;

    auto gb_dsoControl = new QGroupBox("DSO Control");
    auto l_dsoControl = make_hbox<0, 0>(gb_dsoControl);
    l_dsoControl->addWidget(d->dsoControlWidget);

    auto gb_traceSelect = new QGroupBox("Trace+Trigger Selection");
    auto l_traceSelect = make_hbox<0, 0>(gb_traceSelect);
    l_traceSelect->addWidget(d->traceSelectWidget);

    d->label_status = new QLabel;
    auto pb_debugNext = new QPushButton("Debug next buffer");

    auto l_status = make_hbox();
    l_status->addWidget(d->label_status, 1);
    l_status->addWidget(pb_debugNext);

    auto w_left = new QWidget;
    auto l_left = make_vbox<0, 0>(w_left);
    l_left->addWidget(gb_dsoControl, 0);
    l_left->addWidget(gb_traceSelect, 1);
    l_left->addLayout(l_status, 0);

    auto splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(w_left);
    splitter->addWidget(d->dsoPlotWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    auto widgetLayout = make_hbox(this);
    widgetLayout->addWidget(splitter);

    setWindowTitle("Trigger IO DSO");

    connect(d->trigIOScript, &VMEScriptConfig::modified,
            this, [this] () {
                d->onTriggerIOModified();
            });

    connect(d->traceSelectWidget, &TraceSelectWidget::selectionChanged,
            this, [this] (const QVector<PinAddress> &) {
                d->updatePlotTraces();
            });

    connect(d->traceSelectWidget, &TraceSelectWidget::triggersChanged,
            this, [this] () {
                d->restartDSO();
                d->updatePlotTraces();
            });

    connect(d->traceSelectWidget, &TraceSelectWidget::debugTraceClicked,
            this, [this] (const PinAddress &pa) {
                d->debugOnTraceClicked(pa);
            });

    connect(d->dsoControlWidget, &DSOControlWidget::startDSO,
            this, [this] () {
                d->startDSO();
            });

    connect(d->dsoControlWidget, &DSOControlWidget::stopDSO,
            this, [this] () {
                d->stopDSO();
            });

    connect(d->dsoControlWidget, &DSOControlWidget::restartDSO,
            this, [this] () {
                d->restartDSO();
            });

    connect(&d->resultWatcher, &QFutureWatcher<DSO_Sim_Result>::finished,
            this, [this] () {
                d->onDSOSimRunFinished();
            });

    connect(pb_debugNext, &QPushButton::clicked,
            this, [this] () {
                d->debugAction = Private::DebugAction::Next;
            });

    connect(d->dsoPlotWidget, &DSOPlotWidget::traceClicked,
            this, [] (const Trace &trace, const QString &name) {
                show_trace_debug_widget(trace, name);
            });

    connect(&d->statusUpdateTimer, &QTimer::timeout,
            this, [this] () {
                d->updateStatusInfo();
            });
    d->statusUpdateTimer.setInterval(500);
    d->statusUpdateTimer.start();

    d->onTriggerIOModified(); // initial data pull from the script
    d->loadGUIState(); // load last saved GUI state from disk
}

DSOSimWidget::~DSOSimWidget()
{
    d->cancelDSO = true;
    d->resultWatcher.waitForFinished();
    d->saveGUIState();
}

void DSOSimWidget::setMVLC(mvlc::MVLC mvlc)
{
    d->stopDSO();

    if (d->resultWatcher.isRunning())
        d->resultWatcher.waitForFinished();

    d->mvlc = mvlc;
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
