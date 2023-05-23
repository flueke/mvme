#include <QApplication>
#include <spdlog/spdlog.h>

#include "mvme_session.h"
#include "replay_ui.h"

using namespace mesytec;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_replay_ui");

    mvme::ReplayWidget replayWidget;
    add_widget_close_action(&replayWidget);
    replayWidget.show();

    if (auto args = app.arguments(); args.size() > 1)
        replayWidget.browsePath(args.at(1));
    else
        replayWidget.browsePath(".");

    return app.exec();
}