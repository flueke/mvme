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

template<typename C>
QStringList to_qstrlist(const C &container)
{
    QStringList result;
    result.reserve(container.size());
    std::copy(container.begin(), container.end(), std::back_inserter(result));
    return result;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Config ioCfg;

    auto logicWidget = new QWidget;
    auto logicLayout = new QHBoxLayout(logicWidget);

    {
        auto scene = new TriggerIOGraphicsScene;
      // Edit LUT
        QObject::connect(scene, &TriggerIOGraphicsScene::editLUT,
                         [&ioCfg] (int level, int unit)
        {
            auto lutName = QString("L%1.LUT%2").arg(level).arg(unit);
            QVector<QStringList> inputNameLists;

            // specific handling for Level1
            if (level == 1
                && 0 <= unit
                && unit < static_cast<int>(Level1::StaticConnections.size()))
            {
                const auto &connections = Level1::StaticConnections[unit];

                for (auto address: connections)
                {
                    // handles static Level1 -> Level0 connections
                    if (address[0] == 0)
                    {
                        inputNameLists.push_back(
                            {ioCfg.l0.outputNames.value(address[1])});
                    }
                    // handles internal Level1 connections
                    else if (address[0] == 1)
                    {
                        inputNameLists.push_back(
                            {ioCfg.l1.luts[address[1]].outputNames[address[2]]});
                    }
                }
            }
            // Level2
            else if (level == 2
                && 0 <= unit
                && unit < static_cast<int>(Level2::StaticConnections.size()))
            {
                const auto &connections = Level2::StaticConnections[unit];
                const auto l2InputChoices = make_level2_input_choices(unit);

                for (size_t inputIndex = 0; inputIndex < connections.size(); inputIndex++)
                {
                    auto &con = connections[inputIndex];

                    if (!con.isDynamic)
                    {
                        auto name = lookup_name(ioCfg, con.address);
                        inputNameLists.push_back({name});
                    }
                    else if (inputIndex < l2InputChoices.inputChoices.size())
                    {
                        auto choices = l2InputChoices.inputChoices[inputIndex];

                        QStringList choiceNames;

                        for (auto &address: choices)
                        {
                            choiceNames.push_back(lookup_name(ioCfg, address));
                        }

                        inputNameLists.push_back(choiceNames);
                    }
                }
            }


            QStringList outputNames;

            // Level1
            if (level == 1
                && 0 <= unit
                && unit < static_cast<int>(ioCfg.l1.luts.size()))
            {
                outputNames = to_qstrlist(ioCfg.l1.luts[unit].outputNames);
            }
            else if (level == 2
                     && 0 <= unit
                     && unit < static_cast<int>(ioCfg.l2.luts.size()))
             {
                 outputNames = to_qstrlist(ioCfg.l2.luts[unit].outputNames);
             }

            qDebug() << __PRETTY_FUNCTION__ << lutName << inputNameLists;

            // run the editor dialog
            LUTEditor lutEditor(lutName, inputNameLists, outputNames);
            lutEditor.resize(850, 650);
            auto dc = lutEditor.exec();

            // apply changes
            if (dc == QDialog::Accepted)
            {
                auto outputNames = lutEditor.getOutputNames();
                LUT *lut = nullptr;

                if (level == 1)
                    lut = &ioCfg.l1.luts[unit];
                else if (level == 2)
                    lut = &ioCfg.l2.luts[unit];

                size_t count = std::min(lut->outputNames.size(),
                                        static_cast<size_t>(outputNames.size()));

                std::copy_n(outputNames.begin(), count, lut->outputNames.begin());
            }
        });

        // NIM IO Setup
        QObject::connect(scene, &TriggerIOGraphicsScene::editNIM_IOs,
                         [&ioCfg] ()
         {
             QStringList names;

             std::copy_n(ioCfg.l0.outputNames.begin() + ioCfg.l0.NIM_IO_Offset,
                         trigger_io::NIM_IO_Count,
                         std::back_inserter(names));

             QVector<trigger_io::IO> settings;
             std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), std::back_inserter(settings));

             NIM_IO_SettingsDialog dialog(names, settings);
             auto dc = dialog.exec();

             if (dc == QDialog::Accepted)
             {
                 names = dialog.getNames();

                 std::copy_n(names.begin(),
                             trigger_io::NIM_IO_Count,
                             ioCfg.l0.outputNames.begin() + ioCfg.l0.NIM_IO_Offset);

                 settings = dialog.getSettings();
                 size_t count = std::min(static_cast<size_t>(settings.size()), ioCfg.l0.ioNIM.size());
                 std::copy_n(settings.begin(), count, ioCfg.l0.ioNIM.begin());
             }
         });

        auto view = new QGraphicsView(scene);

        view->setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing |
            QPainter::SmoothPixmapTransform |
            QPainter::HighQualityAntialiasing);

        logicLayout->addWidget(view);
    }

    auto mainLayout = new QHBoxLayout;
    mainLayout->addWidget(logicWidget);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->resize(1400, 900);
    mainWindow->show();

    int ret =  app.exec();
    return ret;
}
