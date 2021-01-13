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
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvme_qwt.h"

using namespace mesytec::mvme_mvlc;
using namespace trigger_io_scope;
using namespace trigger_io;
using namespace std::chrono_literals;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);


    int ret = app.exec();
    return ret;
}
