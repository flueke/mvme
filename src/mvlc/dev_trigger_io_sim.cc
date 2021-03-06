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
#include "mvlc/trigger_io_dso_sim_ui.h"
#include "mvlc/trigger_io_dso_sim_ui_p.h"
#include "mvlc/trigger_io_dso_plot_widget.h"
#include "mvlc/trigger_io_sim.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvme_qwt.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec::mvme_mvlc;
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

    // ------------------------------------------------------

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

#if 0
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

    traceSelectWidget.show();
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
#endif
    auto trigIO = load_default_trigger_io();
    TraceSelectWidget traceSelectWidget;
    traceSelectWidget.setTriggerIO(trigIO);
    traceSelectWidget.show();


#if 0
    std::vector<QStandardItem *> leaves;

    find_leave_items(traceTreeModel->invisibleRootItem(), leaves, 1);
    qDebug() << "leaves=" << leaves;
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
    dsoControlWidget.setDSOSettings(dsoSetup.preTriggerTime, dsoSetup.postTriggerTime, interval);
    dsoControlWidget.show();

    traceSelectWidget.setTriggers(get_combined_triggers(dsoSetup));

    QObject::connect(&dsoControlWidget, &DSOControlWidget::startDSO,
                     [&dsoControlWidget] () { dsoControlWidget.setDSOActive(true); });

    QObject::connect(&dsoControlWidget, &DSOControlWidget::stopDSO,
                     [&dsoControlWidget] () { dsoControlWidget.setDSOActive(false); });

    int ret = app.exec();
    return ret;
}
