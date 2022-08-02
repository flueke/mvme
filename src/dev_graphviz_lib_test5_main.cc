#include "dev_graphviz_lib_test5.h"
#include <QApplication>
#include <QTimer>

static const char *DefaultAnalysisFilename = "Comy426-neu-point20-pneu-nHDP_lut_cond.analysis";

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow mainwin;
    mainwin.show();

    QTimer::singleShot(0, [&] { mainwin.openAnalysis(DefaultAnalysisFilename); });

    int ret = app.exec();
    return ret;
}