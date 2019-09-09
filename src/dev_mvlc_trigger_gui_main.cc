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
using namespace mesytec::mvlc::trigger_io_config;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Config ioCfg;

    auto logicWidget = new QWidget;
    auto logicLayout = new QHBoxLayout(logicWidget);

    {
        auto scene = new TriggerIOGraphicsScene;

        QObject::connect(scene, &TriggerIOGraphicsScene::editLUT,
                         [&ioCfg] (int level, int unit)
        {
            auto lutName = QString("L%1.LUT%2").arg(level).arg(unit);
            QStringList inputNames;

            // specific handling for Level1
            if (level == 1
                && 0 <= unit
                && unit < static_cast<int>(Level1::StaticConnections.size()))
            {
                const auto &connections = Level1::StaticConnections[unit];

                for (auto address: connections)
                {
                    // specific handling for Level0
                    if (address[0] == 0)
                    {
                        unsigned nameIndex = Level0::OutputPinMapping[address[1]][1];
                        inputNames.push_back(ioCfg.l0.outputNames.value(nameIndex));
                    }
                }
            }

            QStringList outputNames;

            qDebug() << __PRETTY_FUNCTION__ << lutName << inputNames;

            LUTEditor lutEditor(lutName, inputNames, outputNames);
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
