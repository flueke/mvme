#include <QApplication>
#include <QMainWindow>
#include <QFileDialog>
#include "multi_crate.h"
#include <spdlog/spdlog.h>
#include "mvme_session.h"
#include "analysis/event_builder_monitor.hpp"

using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_some_gui_thing");

    multi_crate::MinimalAnalysisServiceProvider asp;

    analysis::EventBuilderMonitorWidget ebWidget(&asp);
    ebWidget.show();

    return app.exec();
}
