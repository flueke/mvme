/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc/mvlc_trigger_io_editor.h"
#include "mvlc/mvlc_trigger_io_editor_p.h"

#include <QDebug>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <qnamespace.h>

#include "mvlc/trigger_io_dso_sim_ui.h"
#include "mvlc/mvlc_trigger_io_script.h"
#include "mvlc/mvlc_trigger_io_util.h"
#include "qt_assistant_remote_control.h"
#include "template_system.h"
#include "util/algo.h"
#include "util/qt_container.h"
#include "util/qt_monospace_textedit.h"
#include "vme_script_editor.h"
#include "vme_script.h"
#include "vme_script_util.h"

namespace mesytec
{

using namespace mvme_mvlc;
using namespace mvme_mvlc::trigger_io;
using namespace mvme_mvlc::trigger_io_config;

struct MVLCTriggerIOEditor::Private
{
    MVLCTriggerIOEditor *q = nullptr;
    TriggerIO ioCfg;
    VMEScriptConfig *scriptConfig = nullptr;
    QString initialScriptContents;
    VMEScriptEditor *scriptEditor = nullptr;
    TriggerIOGraphicsScene *scene = nullptr;
    bool scriptAutorun = false;
    QStringList vmeEventNames;

    trigger_io::DSOSimWidget *dsoWidget = nullptr;
    mvlc::MVLC mvlc;

    void onActionPrintFrontPanelSetup();
};

void MVLCTriggerIOEditor::Private::onActionPrintFrontPanelSetup()
{
    QString buffer;
    QTextStream stream(&buffer);
    print_front_panel_io_table(stream, ioCfg);

    auto te = mvme::util::make_monospace_plain_textedit().release();
    te->setWindowTitle(QSL("MVLC Front Panel IO Setup"));
    te->setAttribute(Qt::WA_DeleteOnClose);
    te->resize(600, 600);
    te->setPlainText(buffer);
    te->show();
    te->raise();
}

MVLCTriggerIOEditor::MVLCTriggerIOEditor(
    VMEScriptConfig *scriptConfig,
    QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->initialScriptContents = scriptConfig->getScriptContents();
    d->scriptConfig = scriptConfig;

    if (scriptConfig->getScriptContents().isEmpty())
        regenerateScript();
    else
        d->ioCfg = parse_trigger_io_script_text(scriptConfig->getScriptContents());

    auto scene = new TriggerIOGraphicsScene(d->ioCfg);
    d->scene = scene;
    d->scene->setStaticConnectionsVisible(false);
    d->scene->setConnectionBarsVisible(true);

    // Edit LUT
    QObject::connect(scene, &TriggerIOGraphicsScene::editLUT,
                     [this] (int level, int unit)
    {
        auto &ioCfg = d->ioCfg;
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

            for (size_t inputIndex = 0; inputIndex < connections.size(); inputIndex++)
            {
                auto &con = connections[inputIndex];

                if (!con.isDynamic)
                {
                    auto name = lookup_name(ioCfg, con.address);
                    inputNameLists.push_back({name});
                }
                else
                {
                    assert(unit == 2);
                    QStringList choiceNames;
                    for (auto &address: Level1::LUT2DynamicInputChoices[inputIndex])
                        choiceNames.push_back(lookup_name(ioCfg, address));
                    inputNameLists.push_back(choiceNames);
                }
            }
        }
        // Level2
        else if (level == 2
            && 0 <= unit
            && unit < static_cast<int>(Level2::StaticConnections.size()))
        {
            const auto &connections = Level2::StaticConnections[unit];
            const auto &l2InputChoices = Level2::DynamicInputChoices[unit];

            for (size_t inputIndex = 0; inputIndex < connections.size(); inputIndex++)
            {
                auto &con = connections[inputIndex];

                if (!con.isDynamic)
                {
                    auto name = lookup_name(ioCfg, con.address);
                    inputNameLists.push_back({name});
                }
                else if (inputIndex < l2InputChoices.lutChoices.size())
                {
                    auto choices = l2InputChoices.lutChoices[inputIndex];

                    QStringList choiceNames;

                    for (auto &address: choices)
                    {
                        choiceNames.push_back(lookup_name(ioCfg, address));
                    }

                    inputNameLists.push_back(choiceNames);
                }
            }

            for (const auto &address: l2InputChoices.strobeChoices)
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

        // run the editor dialog
        std::unique_ptr<LUTEditor> lutEditor;

        if (level == 1)
        {
            if (unit == 2)
            {
                lutEditor = std::make_unique<LUTEditor>(
                    lutName,
                    ioCfg.l1.luts[unit],
                    inputNameLists,
                    ioCfg.l1.lut2Connections,
                    outputNames,
                    this);
                lutEditor->resize(850, 1000);
            }
            else
            {
                lutEditor = std::make_unique<LUTEditor>(
                    lutName,
                    ioCfg.l1.luts[unit],
                    inputNameLists, outputNames,
                    this);
                lutEditor->resize(850, 650);
            }
        }
        else if (level == 2)
        {
            lutEditor = std::make_unique<LUTEditor>(
                lutName,
                ioCfg.l2.luts[unit],
                inputNameLists,
                ioCfg.l2.lutConnections[unit],
                outputNames,
                strobeInputChoiceNames,
                strobeConValue,
                strobeGGSettings,
                strobedOutputs,
                this);
            lutEditor->resize(850, 1000);
        }

        assert(lutEditor);

        auto do_apply = [this, &lutEditor, &ioCfg, level, unit]
        {
            auto outputNames = lutEditor->getOutputNames();
            LUT *lut = nullptr;

            if (level == 1)
                lut = &ioCfg.l1.luts[unit];
            else if (level == 2)
                lut = &ioCfg.l2.luts[unit];
            else
            {
                InvalidCodePath;
                return;
            }

            size_t count = std::min(lut->outputNames.size(),
                                    static_cast<size_t>(outputNames.size()));

            std::copy_n(outputNames.begin(), count, lut->outputNames.begin());

            lut->lutContents = lutEditor->getLUTContents();

            if (level == 1 && unit == 2)
            {
                ioCfg.l1.lut2Connections = lutEditor->getDynamicConnectionValues();
            }

            if (level == 2)
            {
                ioCfg.l2.lutConnections[unit] = lutEditor->getDynamicConnectionValues();
                ioCfg.l2.strobeConnections[unit] = lutEditor->getStrobeConnectionValue();
                ioCfg.l2.luts[unit].strobeGG = lutEditor->getStrobeSettings();
                ioCfg.l2.luts[unit].strobedOutputs = lutEditor->getStrobedOutputMask();
            }

            setupModified();

            qDebug() << __PRETTY_FUNCTION__;
        };

        connect(lutEditor.get(), &QDialog::accepted, this, do_apply);

        lutEditor->setWindowModality(Qt::WindowModal);
        lutEditor->exec();
    });

    // NIM IO Setup
    QObject::connect(scene, &TriggerIOGraphicsScene::editNIM_Inputs,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

        // read names stored in the Level0 structure
        QStringList names;

        std::copy_n(ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset,
                    trigger_io::NIM_IO_Count,
                    std::back_inserter(names));

        // settings stored in Level0
        QVector<trigger_io::IO> settings;
        std::copy(ioCfg.l0.ioNIM.begin(), ioCfg.l0.ioNIM.end(), std::back_inserter(settings));

        NIM_IO_SettingsDialog dialog(names, settings, this);
        dialog.setWindowModality(Qt::WindowModal);

        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            auto names = dialog.getNames();

            // Copy names to L0
            std::copy_n(names.begin(),
                        trigger_io::NIM_IO_Count,
                        ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset);

            // Copy names to L3
            std::copy_n(names.begin(),
                        trigger_io::NIM_IO_Count,
                        ioCfg.l3.unitNames.begin() + ioCfg.l3.NIM_IO_Unit_Offset);

            auto settings = dialog.getSettings();
            size_t count = std::min(static_cast<size_t>(settings.size()), ioCfg.l0.ioNIM.size());

            // Copy settings to L0 and L3
            std::copy_n(settings.begin(), count, ioCfg.l0.ioNIM.begin());
            std::copy_n(settings.begin(), count, ioCfg.l3.ioNIM.begin());

            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    // IRQ inputs
    QObject::connect(scene, &TriggerIOGraphicsScene::editIRQ_Inputs,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

        // read names stored in the Level0 structure
        QStringList names;

        std::copy_n(ioCfg.l0.unitNames.begin() + ioCfg.l0.IRQ_Inputs_Offset,
                    trigger_io::Level0::IRQ_Inputs_Count,
                    std::back_inserter(names));

        // settings stored in Level0
        QVector<trigger_io::IO> settings;
        std::copy(ioCfg.l0.ioIRQ.begin(), ioCfg.l0.ioIRQ.end(), std::back_inserter(settings));

        IRQ_Inputs_SettingsDialog dialog(names, settings, this);
        dialog.setWindowModality(Qt::WindowModal);

        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            auto names = dialog.getNames();

            // Copy names to L0
            std::copy_n(names.begin(),
                        trigger_io::Level0::IRQ_Inputs_Count,
                        ioCfg.l0.unitNames.begin() + ioCfg.l0.IRQ_Inputs_Offset);

            auto settings = dialog.getSettings();
            size_t count = std::min(static_cast<size_t>(settings.size()), ioCfg.l0.ioIRQ.size());

            // Copy settings to L0
            std::copy_n(settings.begin(), count, ioCfg.l0.ioIRQ.begin());

            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    QObject::connect(scene, &TriggerIOGraphicsScene::editNIM_Outputs,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

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
            const auto &choiceList = ioCfg.l3.DynamicInputChoiceLists[idx][0];

            QStringList nameList;

            for (const auto &address: choiceList)
                nameList.push_back(lookup_name(ioCfg, address));

            inputChoiceNameLists.push_back(nameList);
        }

        auto connections = to_qvector(
            ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset,
            ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset + trigger_io::NIM_IO_Count);

        NIM_IO_SettingsDialog dialog(names, settings, inputChoiceNameLists, connections, this);
        dialog.setWindowModality(Qt::WindowModal);


        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            auto names = dialog.getNames();

            // Copy names to L0
            std::copy_n(names.begin(),
                        trigger_io::NIM_IO_Count,
                        ioCfg.l0.unitNames.begin() + ioCfg.l0.NIM_IO_Offset);

            // Copy names to L3
            std::copy_n(names.begin(),
                        trigger_io::NIM_IO_Count,
                        ioCfg.l3.unitNames.begin() + ioCfg.l3.NIM_IO_Unit_Offset);

            auto settings = dialog.getSettings();
            {
                size_t count = std::min(static_cast<size_t>(settings.size()), ioCfg.l0.ioNIM.size());

                // Copy settings to L0 and L3
                std::copy_n(settings.begin(), count, ioCfg.l0.ioNIM.begin());
                std::copy_n(settings.begin(), count, ioCfg.l3.ioNIM.begin());
            }

            {
                auto connections = dialog.getConnections();
                auto count = std::min(static_cast<size_t>(connections.size()),
                                      trigger_io::NIM_IO_Count);
                std::copy_n(
                    connections.begin(), count,
                    ioCfg.l3.connections.begin() + ioCfg.l3.NIM_IO_Unit_Offset);
            }

            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    QObject::connect(scene, &TriggerIOGraphicsScene::editECL_Outputs,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

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
            const auto &choiceList = ioCfg.l3.DynamicInputChoiceLists[idx][0];

            QStringList nameList;

            for (const auto &address: choiceList)
                nameList.push_back(lookup_name(ioCfg, address));

            inputChoiceNameLists.push_back(nameList);
        }

        auto connections = to_qvector(
            ioCfg.l3.connections.begin() + ioCfg.l3.ECL_Unit_Offset,
            ioCfg.l3.connections.begin() + ioCfg.l3.ECL_Unit_Offset + trigger_io::ECL_OUT_Count);

        ECL_SettingsDialog dialog(names, settings, connections, inputChoiceNameLists, this);
        dialog.setWindowModality(Qt::WindowModal);

        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            auto names = dialog.getNames();

            // Copy names to L3
            std::copy_n(names.begin(),
                        trigger_io::ECL_OUT_Count,
                        ioCfg.l3.unitNames.begin() + ioCfg.l3.ECL_Unit_Offset);

            auto settings = dialog.getSettings();
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

            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    QObject::connect(scene, &TriggerIOGraphicsScene::editL3Utils,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

        QVector<QVector<QStringList>> inputChoiceNameLists;

        // FIXME: Counter latch input hacks all the way
        for (int unit = 0; unit < ioCfg.l3.unitNames.size()-1; unit++)
        {
            if (Level3::CountersOffset <= static_cast<unsigned>(unit)
                && static_cast<unsigned>(unit) < Level3::CountersOffset + Level3::CountersCount)
            {
                QVector<QStringList> foo;

                for (unsigned input=0; input<2; input++)
                {
                    const auto &choiceList = ioCfg.l3.DynamicInputChoiceLists[unit][input];
                    QStringList nameList;
                    for (const auto &address: choiceList)
                        nameList.push_back(lookup_name(ioCfg, address));

                    foo.push_back(nameList);
                }

                inputChoiceNameLists.push_back(foo);
            }
            else
            {
                const auto &choiceList = ioCfg.l3.DynamicInputChoiceLists[unit][0];
                QStringList nameList;
                for (const auto &address: choiceList)
                    nameList.push_back(lookup_name(ioCfg, address));

                inputChoiceNameLists.push_back({nameList});
            }
        }

        Level3UtilsDialog dialog(ioCfg.l3, inputChoiceNameLists, d->vmeEventNames, this);
        dialog.setWindowModality(Qt::WindowModal);
        dialog.resize(1100, 600);

        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            ioCfg.l3 = dialog.getSettings();
            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    QObject::connect(scene, &TriggerIOGraphicsScene::editL0Utils,
                     [this] ()
    {
        auto &ioCfg = d->ioCfg;

        Level0UtilsDialog dialog(ioCfg.l0, d->vmeEventNames, this);
        dialog.setWindowModality(Qt::WindowModal);
        dialog.resize(1300, 600);

        auto do_apply = [this, &dialog, &ioCfg] ()
        {
            ioCfg.l0 = dialog.getSettings();
            setupModified();
        };

        connect(&dialog, &QDialog::accepted, this, do_apply);

        dialog.exec();
    });

    QObject::connect(d->scriptConfig, &VMEScriptConfig::modified,
                     this, &MVLCTriggerIOEditor::reload);

    auto view = new QGraphicsView(scene);
    new MouseWheelZoomer(view, view);

    view->setRenderHints(
        QPainter::Antialiasing | QPainter::TextAntialiasing |
        QPainter::SmoothPixmapTransform |
        QPainter::HighQualityAntialiasing);

    auto logicWidget = new QWidget;
    auto logicLayout = make_vbox<0, 0>(logicWidget);
    logicLayout->addWidget(view, 1);

    auto toolbar = make_toolbar();
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    QAction *action = nullptr;

    action = toolbar->addAction(
        QIcon(":/script-run.png"), QSL("Run"),
        this,  &MVLCTriggerIOEditor::runScript_);

    action = toolbar->addAction(
        QIcon(":/gear--arrow.png"), QSL("Autorun"));

    connect(action, &QAction::toggled, this, [this] (bool b) { d->scriptAutorun = b; });

    action->setCheckable(true);
    action->setChecked(true);

#if 0
    action = toolbar->addAction(
        QIcon(":/document-open.png"), QSL("Load from file"));
    action->setEnabled(false);

    action = toolbar->addAction(
        QIcon(":/document-save-as.png"), QSL("Save to file"));
    action->setEnabled(false);
#endif

    toolbar->addSeparator();

    auto clearMenu = new QMenu;
    clearMenu->addAction(
        QSL("Default Setup"), this, [this] ()
        {
            auto scriptContents = vats::read_default_mvlc_trigger_io_script().contents;
            d->ioCfg = parse_trigger_io_script_text(scriptContents);
            setupModified();
        });

    clearMenu->addAction(
        QSL("Empty Setup"), this, [this] ()
        {
            d->ioCfg = {};
            setupModified();
        });

    action = toolbar->addAction(
        QIcon(":/document-new.png"), QSL("Clear Setup"));

    action->setMenu(clearMenu);

    if (auto clearButton = qobject_cast<QToolButton *>(toolbar->widgetForAction(action)))
        clearButton->setPopupMode(QToolButton::InstantPopup);

    action = toolbar->addAction(
        QIcon(":/application-rename.png"), QSL("Reset Names"),
        this, [this] ()
        {
            reset_names(d->ioCfg);
            setupModified();
        });

    action = toolbar->addAction(
        QIcon(":/document-revert.png"), QSL("Revert to original state"),
        this, [this] ()
        {
            d->scriptConfig->setScriptContents(d->initialScriptContents);
            d->scriptConfig->setModified(false);
            d->ioCfg = parse_trigger_io_script_text(d->scriptConfig->getScriptContents());
            setupModified();
        });

    toolbar->addSeparator();

    // connection map: shows/hides static connection edges and the big bus-like bars
    QSettings settings;
    bool connectionMapVisible = settings.value("MVLC_TriggerIOEditor/ConnectionMapVisible", true).toBool();
    action = toolbar->addAction(QSL("Toggle connection map"));
    action->setCheckable(true);
    action->setChecked(connectionMapVisible);
    action->setToolTip(QSL("Shows/hides fixed connection lines and"
                           " bars and arrows representing connection possibilities."));
    action->setStatusTip(action->toolTip());

    auto show_connect_help = [this, action](bool show)
    {
        d->scene->setStaticConnectionsVisible(show);
        d->scene->setConnectionBarsVisible(show);

        action->setIcon(show
                        ? QIcon(":/resources/layer-visible-on.png")
                        : QIcon(":/resources/layer-visible-off.png"));
        QSettings settings;
        settings.setValue("MVLC_TriggerIOEditor/ConnectionMapVisible", show);
    };

    connect(action, &QAction::triggered, this, show_connect_help);
    show_connect_help(connectionMapVisible);

    // Print a list of the front panel io configuration and names. Could be
    // printed on a piece of paper and used as a cheatsheet when working on the
    // crate.
    action = toolbar->addAction(
        QIcon(QSL(":/document-text.png")),
        QSL("Print Front Panel Setup"),
        this, [this] () { d->onActionPrintFrontPanelSetup(); });

    toolbar->addSeparator();

    action = toolbar->addAction(
        QIcon(":/vme_script.png"), QSL("Edit Script"),
        this, [this] ()
        {
            if (!d->scriptEditor)
            {
                auto editor = new VMEScriptEditor(d->scriptConfig);
                editor->setAttribute(Qt::WA_DeleteOnClose);
                d->scriptEditor = editor;

                connect(editor, &QObject::destroyed,
                        this, [this] () { d->scriptEditor = nullptr; });

                add_widget_close_action(editor);

                auto geoSaver = new WidgetGeometrySaver(editor);
                geoSaver->addAndRestore(editor, "MVLCTriggerIOEditor/CodeEditorGeometry");
            }

            d->scriptEditor->show();
            d->scriptEditor->raise();
        });

    toolbar->addSeparator();

    action = toolbar->addAction(
        QIcon(":/help.png"), QSL("Help"),
        this, [] ()
        {
            mvme::QtAssistantRemoteControl::instance().activateKeyword("mvlc_trigger_io");
        });

    action = toolbar->addAction(
        QIcon(":/vme_event.png"), QSL("DSO"),
        this, [this] ()
        {
            if (!d->dsoWidget)
            {
                d->dsoWidget = new DSOSimWidget(
                    d->scriptConfig,
                    d->mvlc);

                d->dsoWidget->setAttribute(Qt::WA_DeleteOnClose);
                connect(d->dsoWidget, &QObject::destroyed,
                        this, [this] () { d->dsoWidget = nullptr; });
                add_widget_close_action(d->dsoWidget);
                auto geoSaver = new WidgetGeometrySaver(d->dsoWidget);
                geoSaver->addAndRestore(d->dsoWidget, "MVLCTriggerIOEditor/DSOWidgetGeometry");
            }

            d->dsoWidget->show();
            d->dsoWidget->raise();
        });
    action->setToolTip("Digital Storage Oscilloscope");

    auto mainLayout = make_vbox<2, 2>(this);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(logicWidget);

    setWindowTitle(QSL("MVLC Trigger & I/O Editor (")
                   + d->scriptConfig->getVerboseTitle() + ")");
}

MVLCTriggerIOEditor::~MVLCTriggerIOEditor()
{
    if (d->scriptEditor)
        d->scriptEditor->close();

    if (d->dsoWidget)
        d->dsoWidget->close();
}

void MVLCTriggerIOEditor::setVMEEventNames(const QStringList &names)
{
    d->vmeEventNames = names;
}

void MVLCTriggerIOEditor::setMVLC(mvlc::MVLC mvlc)
{
    d->mvlc = mvlc;

    if (d->dsoWidget)
        d->dsoWidget->setMVLC(mvlc);
}

void MVLCTriggerIOEditor::runScript_()
{
    emit runScriptConfig(d->scriptConfig);
}

void MVLCTriggerIOEditor::setupModified()
{
    qDebug() << __PRETTY_FUNCTION__;
    d->scene->setTriggerIOConfig(d->ioCfg);

    regenerateScript();

    if (d->scriptAutorun)
        runScript_();
}

void MVLCTriggerIOEditor::regenerateScript()
{
    qDebug() << __PRETTY_FUNCTION__;
    auto &ioCfg = d->ioCfg;
    auto scriptText = generate_trigger_io_script_text(ioCfg);
    d->scriptConfig->setScriptContents(scriptText);

#ifndef NDEBUG
    {
        auto tmpIoCfg = parse_trigger_io_script_text(d->scriptConfig->getScriptContents());
        auto tmpText = generate_trigger_io_script_text(tmpIoCfg);
        assert(scriptText == tmpText);
    }
#endif
}

void MVLCTriggerIOEditor::reload()
{
    qDebug() << __PRETTY_FUNCTION__;
    d->ioCfg = parse_trigger_io_script_text(d->scriptConfig->getScriptContents());
    d->scene->setTriggerIOConfig(d->ioCfg);
}

} // end namespace mesytec
