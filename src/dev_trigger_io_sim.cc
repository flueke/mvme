#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QFile>

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
    simPlot0.setSnapshot(snapshot, timelineNames);
    //simPlot0.getPlot()->setAxisScale(QwtPlot::xBottom, 0, 120, 5);
    simPlot0.resize(1400, 900);
    simPlot0.show();

    int ret = app.exec();
    return ret;
}
