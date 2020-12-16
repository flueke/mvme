#include <QApplication>
#include <QDebug>
#include <QTimer>

#include <limits>
#include <random>

#include "mvlc/trigger_io_scope.h"
#include "mvlc/trigger_io_scope_ui.h"
#include "mvme_qwt.h"

using namespace mesytec::mvme_mvlc;

int main(int argc, char *argv[])
{
    using namespace trigger_io_scope;

    Snapshot snapshot;
    snapshot.resize(10);
    for (auto &timeline: snapshot)
    {
        timeline.push_back({0, Edge::Rising});
        timeline.push_back({10, Edge::Falling});
        timeline.push_back({20, Edge::Rising});
        timeline.push_back({30, Edge::Falling});
        timeline.push_back({40, Edge::Rising});
        timeline.push_back({50, Edge::Falling});
        timeline.push_back({60, Edge::Rising});
        timeline.push_back({70, Edge::Falling});
    }

    ScopeSetup scopeSetup;
    scopeSetup.preTriggerTime = 40;

    QApplication app(argc, argv);

    ScopePlotWidget plot;
    plot.setSnapshot(scopeSetup, snapshot);

    plot.show();

    auto randomize_data = [] (ScopeSetup &setup, Snapshot &snapshot)
    {
        std::random_device rd;
        std::uniform_int_distribution<> rngEdge(0, 1);
        std::uniform_int_distribution<> rngPreTrigger(0, 40);

        for (auto &timeline: snapshot)
            for (auto &sample: timeline)
                sample.edge = static_cast<Edge>(rngEdge(rd));

        //setup.preTriggerTime = rngPreTrigger(rd);
    };

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&] () {
        randomize_data(scopeSetup, snapshot);
        plot.setSnapshot(scopeSetup, snapshot);
    });

    timer.setInterval(1000);
    timer.start();

    int ret =  app.exec();
    return ret;
}
