#include <QApplication>
#include <QMainWindow>
#include <QFileDialog>
#include "multi_crate.h"
#include <spdlog/spdlog.h>
#include "mvme_session.h"
#include "multiplot_widget.h"

using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::info);
    QApplication app(argc, argv);
    mvme_init("dev_multiplot_widget");

    multi_crate::MinimalAnalysisServiceProvider asp;

    QMainWindow mainWindow;
    auto tb = mainWindow.addToolBar("Foobar");
    auto actionOpen = tb->addAction("Open Sesame");

    auto mpw = new MultiPlotWidget(&asp);
    mainWindow.setCentralWidget(mpw);

    QObject::connect(actionOpen, &QAction::triggered, mpw, [] ()
            {
                auto filename = QFileDialog::getOpenFileName(nullptr, "Open File", QString(), "All Files (*)");
                spdlog::info("selected filename is {}", filename.toStdString());
            });

    mainWindow.show();

    return app.exec();
}
