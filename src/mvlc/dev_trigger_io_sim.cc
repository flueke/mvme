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
#include <qnamespace.h>
#include <random>

#include "mvlc/trigger_io_dso.h"
#include "mvlc/trigger_io_dso_ui.h"
#include "mvlc/trigger_io_sim.h"
#include "mvlc/trigger_io_sim_ui.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvme_qwt.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec::mvme_mvlc;
using namespace trigger_io_dso;
using namespace trigger_io;
using namespace std::chrono_literals;
using std::cout;
using std::endl;

void find_leave_items(const QStandardItem *root, std::vector<QStandardItem *> &dest, int checkColumn = 0)
{
    for (int row = 0; row < root->rowCount(); ++row)
    {
        for (int col = 0; col < root->columnCount(); ++col)
        {
            if (auto child = root->child(row, col))
            {
                if (col == checkColumn && !child->hasChildren())
                    dest.push_back(child);
                else
                    find_leave_items(child, dest, checkColumn);
            }
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    mvme_init("dev_trigger_io_sim");

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

    Trace sysclock;

    simulate_sysclock(sysclock, maxtime);
    auto sysclock_shifted = sysclock;
    for (auto &sample: sysclock_shifted)
        sample.time += SysClockHalfPeriod * 0.25;

    Trace in0, in1, in2, in3, in4, in5, strobeIn,
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

    DSOPlotWidget simPlot0;
    simPlot0.setWindowTitle("Basic LUT");
    simPlot0.setSnapshot(snapshot, 0, timelineNames);
    //simPlot0.getPlot()->setAxisScale(QwtPlot::xBottom, 0, 120, 5);
    simPlot0.resize(1400, 900);
    //simPlot0.show();

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
    auto traceTreeModel = make_trace_tree_model();
    qDebug() << "traceTreeModel mimeTypes:" << traceTreeModel->mimeTypes();
    TraceTreeView traceTreeView;
    traceTreeView.setModel(traceTreeModel.get());

    auto traceTableModel = make_trace_table_model();
    qDebug() << "traceTabelModel mimeTypes:" << traceTableModel->mimeTypes();
    TraceTableView traceTableView;
    traceTableView.setModel(traceTableModel.get());

    traceSelectLayout->addWidget(&traceTreeView);
    traceSelectLayout->addWidget(&traceTableView);

    //traceSelectWidget.show();
    traceSelectWidget.resize(800, 600);

    QObject::connect(
        &traceTableView, &QAbstractItemView::clicked,
        [&] (const QModelIndex &index)
        {
            if (auto item = traceTableModel->itemFromIndex(index))
            {
                qDebug() << "table clicked, item =" << item
                    << ", row =" << index.row()
                    << ", col =" << index.column()
                    << ", data =" << item->data()
                    << item->data().value<PinAddress>()
                    ;
            }
        });

#if 0 // doesn't work for whatever reason. the match code in QAbstractItemModel is rather complex :(
    auto treeMatches = traceTreeModel->match(
        traceTreeModel->indexFromItem(traceTreeModel->invisibleRootItem()), // QModelIndex start
        Qt::UserRole + 1, // role
        QVariant::fromValue(PinAddress({2, 1, 3}, PinPosition::Input)), // value
        -1, // hits
        Qt::MatchRecursive // matchflags
        );
    qDebug() << "treeMatches =" << treeMatches;
#endif

    std::vector<QStandardItem *> leaves;

    find_leave_items(traceTreeModel->invisibleRootItem(), leaves, 1);
    qDebug() << "leaves=" << leaves;

#if 0
    for (auto leave: leaves)
    {
        traceTableModel->appendRow(leave->clone());
        leave->setText("foobar!!!");
    }
#endif

    DSOSetup dsoSetup = {};
    dsoSetup.preTriggerTime = 42;
    dsoSetup.postTriggerTime = 1337;

    for (size_t i=0; i<dsoSetup.nimTriggers.size(); i+=2)
        dsoSetup.nimTriggers.set(i);

    for (size_t i=0; i<dsoSetup.irqTriggers.size(); i+=2)
        dsoSetup.irqTriggers.set(i);

    std::chrono::milliseconds interval(23);


    DSOControlWidget dsoControlWidget;
    dsoControlWidget.setDSOSetup(dsoSetup, interval);
    dsoControlWidget.show();

    QObject::connect(&dsoControlWidget, &DSOControlWidget::startDSO,
                     [&dsoControlWidget] () { dsoControlWidget.setDSOActive(true); });

    QObject::connect(&dsoControlWidget, &DSOControlWidget::stopDSO,
                     [&dsoControlWidget] () { dsoControlWidget.setDSOActive(false); });

    int ret = app.exec();
    return ret;
}
