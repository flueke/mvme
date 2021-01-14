#include <QApplication>
#include <QDebug>
#include <QTimer>

#include <chrono>
#include <iostream>
#include <limits>
#include <random>

#include "mvlc/trigger_io_scope.h"
#include "mvlc/trigger_io_scope_ui.h"
#include "mvlc/trigger_io_sim.h"
#include "mvme_qwt.h"

using namespace mesytec::mvme_mvlc;
using namespace std::chrono_literals;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    using namespace trigger_io_scope;
    using namespace trigger_io;

    QApplication app(argc, argv);

    //
    // Scope plot with randomizing edges
    //
    Snapshot snapshot;
    snapshot.resize(1);
    for (auto &timeline: snapshot)
    {
        timeline.push_back({ 0ns, Edge::Rising});
        timeline.push_back({10ns, Edge::Falling});
        timeline.push_back({20ns, Edge::Rising});
        timeline.push_back({30ns, Edge::Falling});
        timeline.push_back({40ns, Edge::Rising});
        timeline.push_back({50ns, Edge::Falling});
        timeline.push_back({60ns, Edge::Rising});
        timeline.push_back({70ns, Edge::Falling});
    }


    ScopePlotWidget plotWidget;
    plotWidget.setWindowTitle("Randomizing Edges");
    plotWidget.setSnapshot(snapshot);
    //plotWidget.show();

    auto randomize_data = [] (Snapshot &snapshot)
    {
        std::random_device rd;
        std::uniform_int_distribution<> rngEdge(0, 1);

        for (auto &timeline: snapshot)
            for (auto &sample: timeline)
                sample.edge = static_cast<Edge>(rngEdge(rd));
    };

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&] () {
        randomize_data(snapshot);
        plotWidget.setSnapshot(snapshot);
    });

    timer.setInterval(1000);
    timer.start();


    //
    // IO sim testing 0
    //

    IO io;
    io.invert = true;
    io.delay = 10;
    io.width = 20;
    io.holdoff = 30;

    Timeline input {
        { 0ns,  Edge::Falling }, // initial state

        { 10ns, Edge::Rising },
        { 15ns,  Edge::Falling },

        { 35ns, Edge::Rising },
        { 40ns,  Edge::Falling },

        { 50ns, Edge::Rising },
        { 55ns,  Edge::Falling },
    };

    Timeline output;

    auto simulateUpTo = SampleTime::max();
    simulateUpTo = 46ns; // 45 ignores the last input pulse, 46 uses it

    simulate(io, input, output, simulateUpTo);

    Timeline sysclock;

    simulate_sysclock(sysclock, 1000ns);

    cout << "sysclock: ";
    print(cout, sysclock) << endl;

    Timer timer0;
    timer0.delay_ns = 10;
    timer0.period = 5;
    timer0.range = Timer::Range::ns;
    Timeline timer0Samples;
    simulate(timer0, timer0Samples, 1000ns);

    Snapshot ioSnap = { input, output, timer0Samples };

    ScopePlotWidget simPlot0;
    simPlot0.setWindowTitle("TrigIO Test 0");
    simPlot0.setSnapshot(ioSnap);
    simPlot0.getPlot()->setAxisScale(QwtPlot::xBottom, 0, 120, 5);
    simPlot0.show();

    int ret = app.exec();
    return ret;
}
