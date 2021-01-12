#include <QApplication>
#include <QDebug>
#include <QTimer>

#include <chrono>
#include <limits>
#include <random>

#include "mvlc/trigger_io_scope.h"
#include "mvlc/trigger_io_scope_ui.h"
#include "mvlc/mvlc_trigger_io_sim.h"
#include "mvme_qwt.h"

using namespace mesytec::mvme_mvlc;
using namespace std::chrono_literals;

int main(int argc, char *argv[])
{
    using namespace trigger_io_scope;
    using namespace trigger_io;

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

    ScopeSetup scopeSetup;
    scopeSetup.preTriggerTime = 30;
    scopeSetup.postTriggerTime = 40;

    QApplication app(argc, argv);

    ScopePlotWidget plotWidget;
    plotWidget.setWindowTitle("Randomizing Edges");
    plotWidget.setSnapshot(scopeSetup, snapshot);
    plotWidget.show();

    auto randomize_data = [] (ScopeSetup &setup, Snapshot &snapshot)
    {
        std::random_device rd;
        std::uniform_int_distribution<> rngEdge(0, 1);

        for (auto &timeline: snapshot)
            for (auto &sample: timeline)
                sample.edge = static_cast<Edge>(rngEdge(rd));
    };

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&] () {
        randomize_data(scopeSetup, snapshot);
        plotWidget.setSnapshot(scopeSetup, snapshot);
    });

    timer.setInterval(1000);
    timer.start();


    // IO sim testing

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

    auto simulateUpTo = std::chrono::nanoseconds::max();
    simulateUpTo = 46ns; // 45 ignoeres the last input pulse, 46 uses it

    simulate(io, input, output, simulateUpTo);

    Snapshot ioSnap = { input, output };
    ScopeSetup ioScopeSetup = { 0, static_cast<u16>(std::max(input.back().time, output.back().time).count()) };
    ioScopeSetup.postTriggerTime += 20;

    ScopePlotWidget ioPlot;
    ioPlot.setWindowTitle("IO Test");
    ioPlot.setSnapshot(ioScopeSetup, ioSnap);
    ioPlot.getPlot()->setAxisScale(QwtPlot::xBottom, 0, 100, 5);

    ioPlot.show();


    int ret = app.exec();
    return ret;
}
