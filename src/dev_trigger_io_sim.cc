#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QTableView>

#include <chrono>
#include <iostream>
#include <limits>
#include <random>

#include "mvlc/trigger_io_scope.h"
#include "mvlc/trigger_io_scope_ui.h"
#include "mvlc/trigger_io_sim.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvme_qwt.h"
#include "qt_util.h"

using namespace mesytec::mvme_mvlc;
using namespace trigger_io_scope;
using namespace trigger_io;
using namespace std::chrono_literals;
using std::cout;
using std::endl;

std::unique_ptr<QStandardItemModel> make_trace_tree_model(const TriggerIO &trigIO)
{
    using Item = QStandardItem;
    using ItemList = QList<Item *>;

    auto make_item = [] (const QString &name) -> Item *
    {
        auto item = new Item(name);
        item->setEditable(false);
        item->setDragEnabled(false);
        return item;
    };

    auto make_trace_item = [&make_item] (const QString &name) -> Item *
    {
        auto item = make_item(name);
        item->setDragEnabled(true);
        return item;
    };

    auto make_lut_item = [&make_item, &make_trace_item]
        (const QString &name, bool hasStrobe) -> Item *
    {
        auto lutRoot = make_item(name);

        for (auto i=0; i<LUT::InputBits; ++i)
        {
            auto item = make_trace_item(QString("in%1").arg(i));
            lutRoot->appendRow(item);
        }

        if (hasStrobe)
            lutRoot->appendRow(make_trace_item("strobeIn"));

        for (auto i=0; i<LUT::OutputBits; ++i)
        {
            auto item = make_trace_item(QString("out%1").arg(i));
            lutRoot->appendRow(item);
        }

        if (hasStrobe)
            lutRoot->appendRow(make_trace_item("strobeOut"));

        return lutRoot;
    };

    auto model = std::make_unique<QStandardItemModel>();
    auto root = model->invisibleRootItem();

    // Sampled Traces
    auto samplesRoot = make_item("Sampled Traces");
    root->appendRow({ samplesRoot, nullptr });

    for (auto i=0; i<NIM_IO_Count; ++i)
    {
        auto item = make_trace_item(QString("NIM%1").arg(i));
        samplesRoot->appendRow(item);
    }

    // L0
    auto l0Root = make_item("L0 internal");
    root->appendRow(l0Root);

    for (auto i=0; i<TimerCount; ++i)
    {
        auto item = make_trace_item(QString("timer%1").arg(i));
        l0Root->appendRow(item);
    }

    auto sysclockItem = make_item("sysclock");
    l0Root->appendRow(sysclockItem);

    for (auto i=0; i<NIM_IO_Count; ++i)
    {
        auto item = make_trace_item(QString("NIM%1").arg(i));
        l0Root->appendRow(item);
    }

    // L1
    auto l1Root = make_item("L1");
    root->appendRow(l1Root);

    for (auto i=0; i<Level1::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item(QString("L1.LUT%1").arg(i), false);
        l1Root->appendRow(lutRoot);
    }

    // L2
    auto l2Root = make_item("L2");
    root->appendRow(l2Root);

    for (auto i=0; i<Level2::LUTCount; ++i)
    {
        auto lutRoot = make_lut_item(QString("L2.LUT%1").arg(i), true);
        l2Root->appendRow(lutRoot);
    }

    // L3
    auto l3Root = make_item("L3 internal");
    root->appendRow(l3Root);

    for (auto i=0; i<NIM_IO_Count; ++i)
    {
        auto item = make_trace_item(QString("NIM%1").arg(i));
        l3Root->appendRow(item);
    }

    for (auto i=0; i<ECL_OUT_Count; ++i)
    {
        auto item = make_trace_item(QString("LVDS%1").arg(i));
        l3Root->appendRow(item);
    }

    // Finalize
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");

    return model;
}

std::unique_ptr<QStandardItemModel> make_trace_table_model()
{
    auto model = std::make_unique<QStandardItemModel>();
    model->setColumnCount(2);
    model->setHeaderData(0, Qt::Horizontal, "Trace");
    model->setHeaderData(1, Qt::Horizontal, "Name");
    return model;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    LUT lut;

    {
        QFile inFile("/home/florian/src/mesytec/mvlc-things/trigio-sim_test_l2.lut0.vmescript");

        if (!inFile.open(QIODevice::ReadOnly))
        {
            cout << "Error opening trigger io setup file "
                << inFile.fileName().toStdString() << " for reading" << endl;
            return 1;
        }

        auto trigIO = parse_trigger_io_script_text(QString::fromUtf8(inFile.readAll()));
        lut = trigIO.l2.luts[0];
    }

    const SampleTime maxtime(1000ns);

    Timeline sysclock;

    simulate_sysclock(sysclock, maxtime);
    auto sysclock_shifted = sysclock;
    for (auto &sample: sysclock_shifted)
        sample.time += SysClockHalfPeriod * 0.25;

    Timeline in0, in1, in2, in3, in4, in5, strobeIn,
             out0, out1, out2, strobeOut;

    in0 = sysclock;
    in1 = sysclock_shifted;
    in2 = sysclock;
    in3 = sysclock;
    in4 = sysclock;
    in5 = sysclock;
    strobeIn = sysclock;

    LUT_Input_Timelines lutInputs = { &in0, &in1, &in2, &in3, &in4, &in5, &strobeIn };
    LUT_Output_Timelines lutOutputs = { &out0, &out1, &out2, &strobeOut };

    simulate(lut, lutInputs, lutOutputs, maxtime);

    // ------------------------------------------------------
    
    Snapshot snapshot = {
        in0, in1, in2, in3, in4, in5, strobeIn,
        out0, out1, out2, strobeOut
    };

    QStringList timelineNames = {
        "in0", "in1", "in2", "in3", "in4", "in5", "strobeIn",
        "out0", "out1", "out2", "strobeOut"
    };

    ScopePlotWidget simPlot0;
    simPlot0.setWindowTitle("Basic LUT");
    simPlot0.setSnapshot(snapshot, 0, timelineNames);
    //simPlot0.getPlot()->setAxisScale(QwtPlot::xBottom, 0, 120, 5);
    simPlot0.resize(1400, 900);
    simPlot0.show();

    // Trace Select Stuff -------------------------------------

// * Sampled traces:
//  - NIM0..NIM13
//  - More things to come
// 
// * All other traces are simulated
// 
// * L0 internal side
//   - NIM0..NIM13 simulated internal side (GG sim)
//   - L0 utils (timers, sysclock, ...)
// 
// * L1
//   - LUT0..5
//     - input0..5
//     - output0..3
//   Note: L1.LUT0.in0 is the same as the internal side of L0.NIM0. This is a
//   static connection.
// 
// * L2
//   - LUT0..1
//     - input0..5, strobeIn
//     - output0..3, strobeOut
//   Note: the static L2 LUT inputs are the same as the outputs of the L1 LUTS.
//   The variable inputs depend on the selected connection.
//
// Thoughts about pin addressing:
// - addressing as is done in the trigger io code consists of only 3 levels
//   (level, unit, output) where the output is only used for the LUTs
//   This means the input pins cannot even be properly addressed.
//   Also there is no way to address the output pins of the L3 NIMs and LVDS
//   units.

    QWidget traceSelectWidget;
    auto traceSelectLayout = make_hbox(&traceSelectWidget);

    TriggerIO trigIO = {};
    auto traceTreeModel = make_trace_tree_model(trigIO);
    QTreeView traceTreeView;
    traceTreeView.setModel(traceTreeModel.get());
    traceTreeView.setExpandsOnDoubleClick(true);
    traceTreeView.setDragEnabled(true);

    auto traceTableModel = make_trace_table_model();
    QTableView traceTableView;
    traceTableView.setModel(traceTableModel.get());
    traceTableView.setDragEnabled(true);

    traceSelectLayout->addWidget(&traceTreeView);
    traceSelectLayout->addWidget(&traceTableView);

    traceSelectWidget.show();

    int ret = app.exec();
    return ret;
}
