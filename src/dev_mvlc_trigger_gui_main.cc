#include <memory>
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
#include "qt_util.h"

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

template<typename Container>
QVector<typename Container::value_type> to_qvector(const Container &c)
{
    QVector<typename Container::value_type> result;

    for (const auto &value: c)
        result.push_back(value);

    return result;
}

template<typename Iter>
QVector<typename std::iterator_traits<Iter>::value_type> to_qvector(
    Iter begin_, const Iter &end_)
{
    QVector<typename std::iterator_traits<Iter>::value_type> result;

    while (begin_ != end_)
    {
        result.push_back(*begin_++);
    }

    return result;
}

template<typename BS>
void copy_bitset(const BS &in, BS &dest)
{
    for (size_t i = 0; i < in.size(); i++)
        dest.set(i, in.test(i));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    TriggerIOConfig ioCfg;

    auto logicWidget = new QWidget;
    auto logicLayout = new QVBoxLayout(logicWidget);

    {
        auto scene = new TriggerIOGraphicsScene;

        // Edit LUT
        QObject::connect(scene, &TriggerIOGraphicsScene::editLUT,
                         [&ioCfg] (int level, int unit)
        {
            auto lutName = QString("L%1.LUT%2").arg(level).arg(unit);
            QVector<QStringList> inputNameLists;
            QStringList strobeInputChoiceNames;
            unsigned strobeConValue = 0u;
            trigger_io::IO strobeGGSettings = {};
            std::bitset<trigger_io::LUT::OutputBits> strobedOutputs;

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
                            {ioCfg.l0.unitNames.value(address[1])});
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

                for (const auto &address: l2InputChoices.strobeInputChoices)
                    strobeInputChoiceNames.push_back(lookup_name(ioCfg, address));

                strobeConValue = ioCfg.l2.strobeConnections[unit];
                strobeGGSettings = ioCfg.l2.luts[unit].strobeGG;
                copy_bitset(ioCfg.l2.luts[unit].strobedOutputs, strobedOutputs);
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
            std::unique_ptr<LUTEditor> lutEditor;

            if (level == 1)
            {
                lutEditor = std::make_unique<LUTEditor>(
                    lutName, inputNameLists, outputNames);
                lutEditor->resize(850, 650);
            }
            else if (level == 2)
            {
                lutEditor = std::make_unique<LUTEditor>(
                    lutName,
                    inputNameLists,
                    ioCfg.l2.lutConnections[unit],
                    outputNames,
                    strobeInputChoiceNames,
                    strobeConValue,
                    strobeGGSettings,
                    strobedOutputs);
                lutEditor->resize(850, 750);
            }

            auto dc = lutEditor->exec();

            // apply changes
            if (dc == QDialog::Accepted)
            {
                auto outputNames = lutEditor->getOutputNames();
                LUT *lut = nullptr;

                if (level == 1)
                    lut = &ioCfg.l1.luts[unit];
                else if (level == 2)
                    lut = &ioCfg.l2.luts[unit];

                size_t count = std::min(lut->outputNames.size(),
                                        static_cast<size_t>(outputNames.size()));

                std::copy_n(outputNames.begin(), count, lut->outputNames.begin());

                lut->lutContents = lutEditor->getLUTContents();

                if (level == 2)
                {
                    ioCfg.l2.lutConnections[unit] = lutEditor->getDynamicConnectionValues();
                    ioCfg.l2.strobeConnections[unit] = lutEditor->getStrobeConnectionValue();
                    ioCfg.l2.luts[unit].strobeGG = lutEditor->getStrobeSettings();
                    ioCfg.l2.luts[unit].strobedOutputs = lutEditor->getStrobedOutputMask();
                }

            }
        });

        // NIM IO Setup
        QObject::connect(scene, &TriggerIOGraphicsScene::editNIM_Inputs,
                         [&ioCfg] ()
        {
            // read names stored in the Level0 structure
             QStringList names;

             std::copy_n(ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset,
                         trigger_io::NIM_IO_Count,
                         std::back_inserter(names));

             // settings stored in Level0
             QVector<trigger_io::IO> settings;
             std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), std::back_inserter(settings));

             NIM_IO_SettingsDialog dialog(names, settings);
             auto dc = dialog.exec();

             if (dc == QDialog::Accepted)
             {
                 names = dialog.getNames();

                 // Copy names to L0
                 std::copy_n(names.begin(),
                             trigger_io::NIM_IO_Count,
                             ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset);

                 settings = dialog.getSettings();
                 size_t count = std::min(static_cast<size_t>(settings.size()), ioCfg.l0.ioNIM.size());

                 // Copy settings to L0 and L3
                 std::copy_n(settings.begin(), count, ioCfg.l0.ioNIM.begin());
                 std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), ioCfg.l3.ioNIM.begin());
             }
         });

        QObject::connect(scene, &TriggerIOGraphicsScene::editNIM_Outputs,
                         [&ioCfg] ()
        {
            // read names stored in the Level0 structure
             QStringList names;

             std::copy_n(ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset,
                         trigger_io::NIM_IO_Count,
                         std::back_inserter(names));

             // settings stored in Level3
             QVector<trigger_io::IO> settings;
             std::copy(ioCfg.l3.ioNIM.begin(), ioCfg.l3.ioNIM.end(),
                       std::back_inserter(settings));

             // build a vector of available input names for each NIM IO
             QVector<QStringList> inputChoiceNameLists;

             for (size_t io = 0; io < trigger_io::NIM_IO_Count; io++)
             {
                 int idx = io + trigger_io::Level3::NIM_IO_Unit_Offset;
                 const auto &choiceList = ioCfg.l3.dynamicInputChoiceLists[idx];

                 QStringList nameList;

                 for (const auto &address: choiceList)
                     nameList.push_back(lookup_name(ioCfg, address));

                 inputChoiceNameLists.push_back(nameList);
             }

             auto connections = to_qvector(
                 ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset,
                 ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count);

             NIM_IO_SettingsDialog dialog(names, settings, inputChoiceNameLists, connections);
             auto dc = dialog.exec();

             if (dc == QDialog::Accepted)
             {
                 names = dialog.getNames();

                 // Copy names to L0
                 std::copy_n(names.begin(),
                             trigger_io::NIM_IO_Count,
                             ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset);

                 // Copy names to L3
                 std::copy_n(names.begin(),
                             trigger_io::NIM_IO_Count,
                             ioCfg.l3.unitNames.begin() + ioCfg.l3.NIM_IO_Unit_Offset);

                 settings = dialog.getSettings();
                 {
                     size_t count = std::min(static_cast<size_t>(settings.size()),
                                             ioCfg.l0.ioNIM.size());

                     // Copy settings to L0 and L3
                     std::copy_n(settings.begin(), count, ioCfg.l0.ioNIM.begin());
                     std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), ioCfg.l3.ioNIM.begin());
                 }

                 {
                     auto connections = dialog.getConnections();
                     auto count = std::min(static_cast<size_t>(connections.size()),
                                           trigger_io::NIM_IO_Count);
                     std::copy_n(
                         connections.begin(), count,
                         ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset);
                 }
             }
        });

        QObject::connect(scene, &TriggerIOGraphicsScene::editECL_Outputs,
                         [&ioCfg] ()
        {
            QStringList names;

            std::copy_n(ioCfg.l3.unitNames.begin() + ioCfg.l3.ECL_Unit_Offset,
                        trigger_io::ECL_OUT_Count,
                        std::back_inserter(names));

            // settings stored in Level3
            QVector<trigger_io::IO> settings;
            std::copy(ioCfg.l3.ioECL.begin(), ioCfg.l3.ioECL.end(),
                      std::back_inserter(settings));

            // build a vector of available input names for each ECL IO
            QVector<QStringList> inputChoiceNameLists;

            for (size_t io = 0; io < trigger_io::ECL_OUT_Count; io++)
            {
                int idx = io + trigger_io::Level3::ECL_Unit_Offset;
                const auto &choiceList = ioCfg.l3.dynamicInputChoiceLists[idx];

                QStringList nameList;

                for (const auto &address: choiceList)
                    nameList.push_back(lookup_name(ioCfg, address));

                inputChoiceNameLists.push_back(nameList);
            }

            auto connections = to_qvector(
                ioCfg.l3.connections.begin() + ioCfg.l3.ECL_Unit_Offset,
                ioCfg.l3.connections.begin() + ioCfg.l3.ECL_Unit_Offset + trigger_io::ECL_OUT_Count);

            ECL_SettingsDialog dialog(names, settings, connections, inputChoiceNameLists);
            auto dc = dialog.exec();

            if (dc == QDialog::Accepted)
            {
                 names = dialog.getNames();

                 // Copy names to L0
                 std::copy_n(names.begin(),
                             trigger_io::ECL_OUT_Count,
                             ioCfg.l0.unitNames.begin() + ioCfg.l0.ECL_Unit_Offset);

                 // Copy names to L3
                 std::copy_n(names.begin(),
                             trigger_io::ECL_OUT_Count,
                             ioCfg.l3.unitNames.begin() + ioCfg.l3.ECL_Unit_Offset);

                 settings = dialog.getSettings();
                 {
                     size_t count = std::min(static_cast<size_t>(settings.size()),
                                             ioCfg.l3.ioECL.size());

                     // Copy settings to L3
                     std::copy_n(settings.begin(), count, ioCfg.l3.ioECL.begin());
                 }

                 {
                     auto connections = dialog.getConnections();
                     auto count = std::min(static_cast<size_t>(connections.size()),
                                           trigger_io::ECL_OUT_Count);
                     std::copy_n(
                         connections.begin(), count,
                         ioCfg.l3.connections.begin() + ioCfg.l3.ECL_Unit_Offset);
                 }
            }
        });

        QObject::connect(scene, &TriggerIOGraphicsScene::editL3Utils,
                         [&ioCfg] ()
        {
            QVector<QStringList> inputChoiceNameLists;

            for (int unit = 0; unit < ioCfg.l3.unitNames.size(); unit++)
            {
                const auto &choiceList = ioCfg.l3.dynamicInputChoiceLists[unit];
                QStringList nameList;

                for (const auto &address: choiceList)
                    nameList.push_back(lookup_name(ioCfg, address));

                inputChoiceNameLists.push_back(nameList);
            }

            Level3UtilsDialog dialog(ioCfg.l3, inputChoiceNameLists);
            auto dc = dialog.exec();

            if (dc == QDialog::Accepted)
            {
                ioCfg.l3 = dialog.getSettings();
            }
        });

        QObject::connect(scene, &TriggerIOGraphicsScene::editL0Utils,
                         [&ioCfg] ()
        {
            Level0UtilsDialog dialog(ioCfg.l0);
            auto dc = dialog.exec();
            if (dc == QDialog::Accepted)
            {
                ioCfg.l0 = dialog.getSettings();
            }
        });

        auto view = new TriggerIOView(scene);

        view->setRenderHints(
            QPainter::Antialiasing | QPainter::TextAntialiasing |
            QPainter::SmoothPixmapTransform |
            QPainter::HighQualityAntialiasing);

        auto pb_generateScript = new QPushButton("Generate Script");

        auto bottomLayout = new QHBoxLayout;
        bottomLayout->addStretch(1);
        bottomLayout->addWidget(pb_generateScript);

        logicLayout->addWidget(view, 1);
        logicLayout->addLayout(bottomLayout, 0);

        QTextEdit *te_script = nullptr;

        QObject::connect(pb_generateScript, &QPushButton::clicked,
                [&ioCfg, &te_script] ()
        {
            auto script = generate_trigger_io_script_text(ioCfg);

            if (!te_script)
            {
                te_script = new QTextBrowser();
                te_script->setAttribute(Qt::WA_DeleteOnClose);
                te_script->resize(500, 900);

                auto font = QFont("Monospace", 8);
                font.setStyleHint(QFont::Monospace);
                font.setFixedPitch(true);
                te_script->setFont(font);

                QObject::connect(te_script, &QObject::destroyed,
                                 [&te_script] () { te_script = nullptr; });
            }

            te_script->setText(script);
            te_script->show();
            te_script->raise();
        });
    }

    auto mainLayout = make_hbox<0, 0>();
    mainLayout->addWidget(logicWidget);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->resize(1400, 980);
    mainWindow->show();

    int ret =  app.exec();
    return ret;
}
