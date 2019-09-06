#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QTableWidget>
#include <QWidget>
#include <QCheckBox>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QSplitter>
#include <QDebug>

#include "dev_mvlc_trigger_gui.h"
#include "mvlc/mvlc_trigger_io.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    auto logicWidget = new QWidget;
    auto logicLayout = new QHBoxLayout(logicWidget);

    {
        auto scene = new TriggerIOGraphicsScene;

        QObject::connect(scene, &TriggerIOGraphicsScene::editLUT,
                         [] (int level, int unit)
        {
            LUTEditor lutEditor;
            lutEditor.exec();
        });

        auto view = new QGraphicsView(scene);

        view->setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing |
            QPainter::SmoothPixmapTransform |
            QPainter::HighQualityAntialiasing);

        logicLayout->addWidget(view);
    }

    auto ioSettingsWidget = new IOSettingsWidget;

    auto splitter = new QSplitter;
    splitter->addWidget(ioSettingsWidget);
    splitter->addWidget(logicWidget);

    auto mainLayout = new QHBoxLayout;
    mainLayout->addWidget(splitter);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->show();

    int ret =  app.exec();
    return ret;
}
