/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "mvlc/mvlc_trigger_io_editor.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvme_session.h"
#include "qt_util.h"

using namespace mesytec::mvme_mvlc;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Among other things this makes the program use its own QSettings
    // file/registry key so we don't mess with other programs settings.
    mvme_init("dev_mvlc_trigger_gui");

    auto scriptConfig = std::make_unique<VMEScriptConfig>();
    auto trigger_io_editor = new mesytec::MVLCTriggerIOEditor(scriptConfig.get());

    auto mainLayout = make_hbox<0, 0>();
    mainLayout->addWidget(trigger_io_editor);

    auto mainWindow = new QWidget;
    mainWindow->setLayout(mainLayout);
    mainWindow->setAttribute(Qt::WA_DeleteOnClose);
    mainWindow->resize(1400, 980);
    mainWindow->show();

    // Quit using Ctrl+Q
    auto actionQuit = new QAction("&Quit", mainWindow);
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);
    QObject::connect(actionQuit, &QAction::triggered,
                     mainWindow, [mainWindow] () {
                         mainWindow->close();
                         QApplication::quit();
                     });
    mainWindow->addAction(actionQuit);

    // Save and restore window geometries
    WidgetGeometrySaver widgetGeometrySaver;
    widgetGeometrySaver.addAndRestore(mainWindow, "MainWindow");

    int ret =  app.exec();
    return ret;
}
