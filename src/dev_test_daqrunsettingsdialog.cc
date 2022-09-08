#include <QApplication>
#include <spdlog/spdlog.h>
#include "mvme_session.h"
#include "daqcontrol_widget.h"

int main(int argc, char **argv)
{
    spdlog::set_level(spdlog::level::debug);
    QApplication app(argc, argv);
    mvme_init("dev_test_daqrunsettingsdialog");

    ListFileOutputInfo lfOutInfo;
    lfOutInfo.format = ListFileFormat::ZIP;

    DAQRunSettingsDialog dialog(lfOutInfo);
    dialog.show();

    return app.exec();
}