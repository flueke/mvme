#include <QApplication>
#include <QtConcurrent>
#include <QStateMachine>

#include "multi_crate.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec;
using namespace mesytec::mvlc;
using namespace mesytec::mvme;
using namespace mesytec::mvme::multi_crate;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    spdlog::set_level(spdlog::level::info);

    int ret = app.exec();

    // replay only, no pre/post states to run e.g. after a successful replay
    // where to go from the error state? just restate the machine
    // initial -> idle
    // idle
    // error
    // [all _error states] -> error
    // startup_error
    // idle -> starting
    // starting -> running
    // starting -> startup_error
    // running  -> finished
    // running  -> stopped
    // running  -> runtime_error
    // finished -> idle or starting?
    // stopped  -> idle or starting?

    return ret;
}
