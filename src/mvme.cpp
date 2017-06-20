/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "mvme.h"

#include "ui_mvme.h"
#include "vmusb.h"
#include "vmusb_firmware_loader.h"
#include "mvme_context.h"
#include "vmusb_readout_worker.h"
#include "config_ui.h"
#include "mvme_listfile.h"
#include "vme_config_tree.h"
#include "vme_script_editor.h"
#include "vme_debug_widget.h"
#include "daqcontrol_widget.h"
#include "daqstats_widget.h"
#include "mesytec_diagnostics.h"
#include "mvme_event_processor.h"
#include "gui_util.h"
#include "analysis/analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "analysis/analysis_ui.h"
#include "qt_util.h"
#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif

#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QtGui>
#include <QToolBar>

#include <qwt_plot_curve.h>
#include <quazip/quazipfile.h>

using namespace vats;

static QString make_zip_error(const QString &message, const QuaZip *zip)
{
  auto m = QString("%1\narchive=%2, code=%3")
      .arg(message)
      .arg(zip->getZipName())
      .arg(zip->getZipError());

  return m;
}

static QString make_zip_error(const QString &message, QuaZipFile *zipFile)
{
    auto m = QString("%1\narchive=%2, file=%3, code=%4")
        .arg(message)
        .arg(zipFile->getZipName())
        .arg(zipFile->getFileName())
        .arg(zipFile->getZipError())
        ;

    return m;
}

mvme::mvme(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::mvme)
    , m_context(new MVMEContext(this, this))
    , m_geometrySaver(new WidgetGeometrySaver(this))
{
    qDebug() << "main thread: " << QThread::currentThread();

    connect(m_context, &MVMEContext::daqConfigFileNameChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::modeChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &mvme::onConfigChanged);
    connect(m_context, &MVMEContext::objectAboutToBeRemoved, this, &mvme::onObjectAboutToBeRemoved);
    connect(m_context, &MVMEContext::daqAboutToStart, this, &mvme::onDAQAboutToStart);
    connect(m_context, &MVMEContext::daqStateChanged, this, &mvme::onDAQStateChanged);
    connect(m_context, &MVMEContext::sigLogMessage, this, &mvme::appendToLog);

    connect(m_context, &MVMEContext::daqStateChanged, this, &mvme::updateActions);
    connect(m_context, &MVMEContext::eventProcessorStateChanged, this, &mvme::updateActions);
    connect(m_context, &MVMEContext::modeChanged, this, &mvme::updateActions);
    //connect(m_context, &MVMEContext::controllerStateChanged, this, &mvme::updateActions);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &mvme::updateActions);

    // check and initialize VME interface
    VMEController *controller = new VMUSB;
    m_context->setController(controller); // The context take ownership

    // create and initialize displays
    ui->setupUi(this);

    //
    // central widget consisting of DAQControlWidget, DAQConfigTreeWidget and DAQStatsWidget
    //
    {
        m_daqControlWidget = new DAQControlWidget(m_context);
        m_vmeConfigTreeWidget = new VMEConfigTreeWidget(m_context);
        m_daqStatsWidget = new DAQStatsWidget(m_context);

        auto centralLayout = qobject_cast<QVBoxLayout *>(ui->centralWidget->layout());
        Q_ASSERT(centralLayout);

        centralLayout->setContentsMargins(6, 6, 6, 0); // l, t, r, b
        centralLayout->addWidget(m_daqControlWidget);
        centralLayout->addWidget(m_vmeConfigTreeWidget);
        centralLayout->addWidget(m_daqStatsWidget);

        centralLayout->setStretch(1, 1);

        connect(m_vmeConfigTreeWidget, &VMEConfigTreeWidget::showDiagnostics,
                this, &mvme::onShowDiagnostics);
    }

    updateWindowTitle();

    // Code to run on entering the event loop for the first time.
    QTimer::singleShot(0, [this] () {
        updateActions();

        // Create and open log and analysis windows.
        on_actionLog_Window_triggered();
        on_actionAnalysis_UI_triggered();
        this->raise(); // Focus the main window


        // Open the last workspace or create a new one.

        QSettings settings;
        if (settings.contains(QSL("LastWorkspaceDirectory")))
        {
            try
            {
                m_context->openWorkspace(settings.value(QSL("LastWorkspaceDirectory")).toString());
            } catch (const QString &e)
            {
                QMessageBox::warning(this, QSL("Could not open workspace"), QString("Error opening last workspace: %1.").arg(e));
                settings.remove(QSL("LastWorkspaceDirectory"));

                if (!createNewOrOpenExistingWorkspace())
                {
                    // canceled by user
                    close();
                }
            }
        }
        else
        {
            if (!createNewOrOpenExistingWorkspace())
            {
                // canceled by user
                close();
            }
        }
    });
}

mvme::~mvme()
{
    // To avoid a crash on exit if replay is running
    disconnect(m_context, &MVMEContext::daqStateChanged, this, &mvme::onDAQStateChanged);

    auto workspaceDir = m_context->getWorkspaceDirectory();

    if (!workspaceDir.isEmpty())
    {
        QSettings settings;
        settings.setValue("LastWorkspaceDirectory", workspaceDir);
    }

    delete ui;

    qDebug() << __PRETTY_FUNCTION__ << "mvme instance being destroyed";
}

void mvme::loadConfig(const QString &fileName)
{
    m_context->loadVMEConfig(fileName);
}

void mvme::on_actionNewWorkspace_triggered()
{
    auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Create new workspace directory"), startDir);

    if (dirName.isEmpty())
    {
        // Dialog was canceled
        return;
    }

    try
    {
        m_context->newWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error creating workspace: %1").arg(e));
    }
}

void mvme::on_actionOpenWorkspace_triggered()
{
   /* Use the parent directory of last opened workspace as the start directory
    * for browsing. */
    auto startDir = QSettings().value("LastWorkspaceDirectory").toString();

    if (!startDir.isEmpty())
    {
        QDir dir(startDir);
        dir.cdUp();
        startDir = dir.absolutePath();
    }
    else
    {
        startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Select workspace"), startDir);

    if (dirName.isEmpty())
        return;

    try
    {
        m_context->openWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error opening workspace: %1").arg(e));
    }
}

void mvme::displayAbout()
{
    auto dialog = new QDialog(this);
    dialog->setWindowTitle(QSL("About mvme"));

    auto tb_license = new QTextBrowser(dialog);
    tb_license->setWindowFlags(Qt::Window);
    tb_license->setWindowTitle(QSL("mvme license"));

    {
        QFile licenseFile(":/gpl-notice.txt");
        licenseFile.open(QIODevice::ReadOnly);
        tb_license->setText(licenseFile.readAll());
    }

    auto layout = new QVBoxLayout(dialog);

    {
        auto label = new QLabel;
        label->setPixmap(QPixmap(":/mesytec-logo.png").
                              scaledToWidth(300, Qt::SmoothTransformation));
        layout->addWidget(label);
    }

    {
        QString text = QString("mvme - %1").arg(GIT_VERSION);
        auto label = new QLabel;
        auto font = label->font();
        font.setPointSize(15);
        font.setBold(true);
        label->setFont(font);
        layout->addWidget(label);
    }

    layout->addWidget(new QLabel(QSL("mvme - VME Data Acquisition")));
    layout->addWidget(new QLabel(QString("Version %1").arg(GIT_VERSION)));
    layout->addWidget(new QLabel(QSL("© 2015-2017 mesytec GmbH & Co. KG")));
    layout->addWidget(new QLabel(QSL("Authors: F. Lüke, R. Schneider")));

    {
        QString text(QSL("<a href=\"mailto:info@mesytec.com\">info@mesytec.com</a> - <a href=\"http://www.mesytec.com\">www.mesytec.com</a>"));
        auto label = new QLabel(text);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
    }

    layout->addSpacing(20);

    auto buttonLayout = new QHBoxLayout;

    {
        auto button = new QPushButton(QSL("&License"));
        connect(button, &QPushButton::clicked, this, [this, tb_license]() {
            auto sz = tb_license->size();
            sz = sz.expandedTo(QSize(500, 300));
            tb_license->resize(sz);
            tb_license->show();
            tb_license->raise();
        });

        buttonLayout->addWidget(button);
    }

    {
        auto button = new QPushButton(QSL("&Close"));
        connect(button, &QPushButton::clicked, dialog, &QDialog::close);
        button->setAutoDefault(true);
        button->setDefault(true);
        buttonLayout->addWidget(button);
    }

    layout->addLayout(buttonLayout);

    for (int i=0; i<layout->count(); ++i)
    {
        auto item = layout->itemAt(i);
        item->setAlignment(Qt::AlignHCenter);

        auto label = qobject_cast<QLabel *>(item->widget());
        if (label)
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }

    dialog->exec();
}

void mvme::displayAboutQt()
{
    QMessageBox::aboutQt(this, QSL("About Qt"));
}

static const QString DefaultAnalysisFileFilter = QSL("Config Files (*.json);; All Files (*.*)");

void mvme::closeEvent(QCloseEvent *event)
{
    if (m_context->getDAQState() != DAQState::Idle && m_context->getMode() == GlobalMode::DAQ)
    {
        QMessageBox msgBox(QMessageBox::Warning, QSL("DAQ is running"),
                           QSL("Data acquisition is currently active. Ignoring request to exit."),
                           QMessageBox::Ok);
        msgBox.exec();
        event->ignore();
        return;
    }

    /* Try to close all top level windows except our own window. This will
     * trigger any reimplementations of closeEvent() and thus give widgets a
     * chance to ask the user about how to handle pending modifications. If the
     * QCloseEvent is ignored by the widget the QWindow::close() call will
     * return false. In this case we keep this widget open and ignore our
     * QCloseEvent aswell.  */
    bool allWindowsClosed = true;

    for (auto window: QGuiApplication::topLevelWindows())
    {
        if (window != this->windowHandle())
        {
            if (!window->close())
            {
                qDebug() << __PRETTY_FUNCTION__ << "window" << window << "refused to close";
                allWindowsClosed = false;
                break;
            }
        }
    }

    if (!allWindowsClosed)
    {
        event->ignore();
        return;
    }

    // DAQConfig
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save DAQ configuration?"),
                           QSL("The current DAQ configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveVMEConfig_triggered())
            {
                event->ignore();
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    // AnalysisConfig
    auto analysis = m_context->getAnalysis();
    if (analysis->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis config?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(m_context->getAnalysis(),
                                             m_context->getAnalysisConfigFileName(),
                                             m_context->getWorkspaceDirectory(),
                                             DefaultAnalysisFileFilter,
                                             m_context);
            if (!result.first)
            {
                event->ignore();
                return;
            }
            m_context->setAnalysisConfigFileName(result.second);
        }
        else if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    // window sizes and positions
    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());

    m_quitting = true;

    QMainWindow::closeEvent(event);
    qApp->closeAllWindows();
}

void mvme::restoreSettings()
{
    qDebug("restoreSettings");
    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());
}

void mvme::addObjectWidget(QWidget *widget, QObject *object, const QString &stateKey)
{
    connect(widget, &QObject::destroyed, this, [this, object, widget] (QObject *) {
        m_objectWindows[object].removeOne(widget);
    });

    widget->setAttribute(Qt::WA_DeleteOnClose);
    m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/") + stateKey);
    add_widget_close_action(widget);

    m_objectWindows[object].push_back(widget);
    widget->show();
}

bool mvme::hasObjectWidget(QObject *object) const
{
    return !m_objectWindows[object].isEmpty();
}

QWidget *mvme::getObjectWidget(QObject *object) const
{
    QWidget *result = nullptr;
    const auto &l(m_objectWindows[object]);

    if (!l.isEmpty())
    {
        result = l.last();
    }

    return result;
}

QList<QWidget *> mvme::getObjectWidgets(QObject *object) const
{
    return m_objectWindows[object];
}

void mvme::activateObjectWidget(QObject *object)
{
    if (auto widget = getObjectWidget(object))
    {
        widget->show();
        widget->showNormal();
        widget->raise();
    }
}

void mvme::addWidget(QWidget *widget, const QString &stateKey)
{
    widget->setAttribute(Qt::WA_DeleteOnClose);
    if (!stateKey.isEmpty())
        m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/") + stateKey);
    add_widget_close_action(widget);
    widget->show();
}

void mvme::on_actionNewVMEConfig_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    // TODO: run a dialog to set-up config basics: controller type, working directory, etc...

    m_context->setVMEConfig(new VMEConfig);
    m_context->setConfigFileName(QString());
    m_context->setMode(GlobalMode::DAQ);
}

// Note: .mvmecfg was the old extension when vme and analysis config where not separated yet.
static const QString VMEConfigFileFilter = QSL("Config Files (*.vme *.mvmecfg);; All Files (*.*)");

void mvme::on_actionOpenVMEConfig_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save VME configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    auto path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load MVME Config", path, VMEConfigFileFilter);

    if (fileName.isEmpty())
        return;

    loadConfig(fileName);
}

bool mvme::on_actionSaveVMEConfig_triggered()
{
    if (m_context->getConfigFileName().isEmpty())
    {
        return on_actionSaveVMEConfigAs_triggered();
    }

    QString fileName = m_context->getConfigFileName();
    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_context->getVMEConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    auto config = m_context->getConfig();
    config->setModified(false);
    auto configObjects = config->findChildren<ConfigObject *>();
    for (auto obj: configObjects)
    {
        obj->setModified(false);
    }

    updateWindowTitle();
    return true;
}

bool mvme::on_actionSaveVMEConfigAs_triggered()
{
    QString path = QFileInfo(m_context->getConfigFileName()).absolutePath();

    if (path.isEmpty())
        path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", path, VMEConfigFileFilter);

    if (fileName.isEmpty())
        return false;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += QSL(".vme");
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_context->getVMEConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    m_context->setConfigFileName(fileName);
    m_context->getConfig()->setModified(false);
    updateWindowTitle();
    return true;
}

void mvme::on_actionOpenListfile_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    QString path = m_context->getListFileOutputDirectoryFullPath();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load Listfile",
                                                    path,
                                                    "MVME Listfiles (*.mvmelst *.zip);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    // ZIP
    if (fileName.toLower().endsWith(QSL(".zip")))
    {
        auto inFile = std::make_unique<QuaZipFile>(fileName, QSL("listfile.mvmelst"));

        if (!inFile->open(QIODevice::ReadOnly))
        {
            QMessageBox::critical(0, "Error", make_zip_error("Could not open listfile", inFile.get()));
            return;
        }

        auto listFile = std::make_unique<ListFile>(inFile.release());

        if (!listFile->open())
        {
            QMessageBox::critical(0, "Error", QString("Error opening listfile inside %1 for reading").arg(fileName));
            return;
        }

        auto json = listFile->getDAQConfig();

        if (json.isEmpty())
        {
            QMessageBox::critical(0, "Error", QString("Listfile does not contain a valid DAQ configuration"));
            return;
        }

        bool wasReplaying = (m_context->getMode() == GlobalMode::ListFile
                             && m_context->getDAQState() == DAQState::Running);

        m_context->setReplayFile(listFile.release());

        if (!m_context->getAnalysis()->isModified() && m_context->getAnalysis()->isEmpty())
        {
            QuaZipFile inFile(fileName, QSL("analysis.analysis"));

            if (!inFile.open(QIODevice::ReadOnly))
            {
                QMessageBox::critical(0, "Error", make_zip_error("Could not read analysis file", &inFile));
            }
            else
            {
                m_context->loadAnalysisConfig(&inFile);
            }
        }

        if (wasReplaying)
        {
            m_context->startReplay();
        }
    }
    // Plain
    else
    {
        ListFile *listFile = new ListFile(fileName);

        if (!listFile->open())
        {
            QMessageBox::critical(0, "Error", QString("Error opening %1 for reading").arg(fileName));
            delete listFile;
            return;
        }

        auto json = listFile->getDAQConfig();

        if (json.isEmpty())
        {
            QMessageBox::critical(0, "Error", QString("Listfile does not contain a valid DAQ configuration"));
            delete listFile;
            return;
        }

        bool wasReplaying = (m_context->getMode() == GlobalMode::ListFile
                             && m_context->getDAQState() == DAQState::Running);

        m_context->setReplayFile(listFile);

        if (wasReplaying)
        {
            m_context->startReplay();
        }
    }

    updateWindowTitle();
}

void mvme::on_actionCloseListfile_triggered()
{
    m_context->closeReplayFile();
}

void mvme::on_actionMainWindow_triggered()
{
    raise();
    showNormal();
    show();
}

void mvme::on_actionAnalysis_UI_triggered()
{
    auto analysisUi = m_context->getAnalysisUi();

    if (!analysisUi)
    {
        analysisUi = new analysis::AnalysisWidget(m_context);
        m_context->setAnalysisUi(analysisUi);

        connect(analysisUi, &QObject::destroyed, this, [this] (QObject *) {
            this->m_context->setAnalysisUi(nullptr);
        });

        add_widget_close_action(analysisUi);
        m_geometrySaver->addAndRestore(analysisUi, QSL("WindowGeometries/AnalysisUI"));
        analysisUi->setAttribute(Qt::WA_DeleteOnClose);
    }

    analysisUi->show();
    analysisUi->showNormal();
    analysisUi->raise();
}

void mvme::on_actionVME_Debug_triggered()
{
    if (!m_vmeDebugWidget)
    {
        m_vmeDebugWidget = new VMEDebugWidget(m_context);
        m_vmeDebugWidget->setAttribute(Qt::WA_DeleteOnClose);

        connect(m_vmeDebugWidget, &QObject::destroyed, this, [this] (QObject *) {
            this->m_vmeDebugWidget = nullptr;
        });

        add_widget_close_action(m_vmeDebugWidget);
        m_geometrySaver->addAndRestore(m_vmeDebugWidget, QSL("WindowGeometries/VMEDebug"));
    }

    m_vmeDebugWidget->show();
    m_vmeDebugWidget->showNormal();
    m_vmeDebugWidget->raise();
}

static const size_t LogViewMaximumBlockCount = Megabytes(1);

void mvme::on_actionLog_Window_triggered()
{
    if (!m_logView)
    {
        m_logView = new QPlainTextEdit;
        m_logView->setAttribute(Qt::WA_DeleteOnClose);
        m_logView->setReadOnly(true);
        m_logView->setWindowTitle("Log View");
        QFont font("MonoSpace");
        font.setStyleHint(QFont::Monospace);
        m_logView->setFont(font);
        m_logView->setTabChangesFocus(true);
        m_logView->document()->setMaximumBlockCount(LogViewMaximumBlockCount);
        m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_logView->setStyleSheet("background-color: rgb(225, 225, 225);");
        add_widget_close_action(m_logView);

        connect(m_logView, &QWidget::customContextMenuRequested, this, [=](const QPoint &pos) {
            auto menu = m_logView->createStandardContextMenu(pos);
            auto action = menu->addAction("Clear");
            connect(action, &QAction::triggered, m_logView, &QPlainTextEdit::clear);
            menu->exec(m_logView->mapToGlobal(pos));
            menu->deleteLater();
        });
        connect(m_logView, &QObject::destroyed, this, [this] (QObject *) {
            this->m_logView = nullptr;
        });

        m_geometrySaver->addAndRestore(m_logView, QSL("WindowGeometries/LogView"));
    }

    m_logView->show();
    m_logView->showNormal();
    m_logView->raise();
}

void mvme::on_actionVMUSB_Firmware_Update_triggered()
{
    vmusb_gui_load_firmware(m_context);
}

void mvme::on_actionTemplate_Info_triggered()
{
    QString buffer;
    QTextStream logStream(&buffer);

    logStream << "Reading templates..." << endl;
    MVMETemplates templates = read_templates([&logStream](const QString &msg) {
        logStream << msg << endl;
    });

    logStream << endl << templates;

    auto textEdit = new QPlainTextEdit;
    textEdit->setAttribute(Qt::WA_DeleteOnClose);
    textEdit->setReadOnly(true);
    textEdit->setWindowTitle("VME/Analysis Template System Info");
    QFont font("MonoSpace");
    font.setStyleHint(QFont::Monospace);
    textEdit->setFont(font);
    textEdit->setTabChangesFocus(true);
    textEdit->resize(600, 500);
    textEdit->setPlainText(buffer);
    add_widget_close_action(textEdit);
    m_geometrySaver->addAndRestore(textEdit, QSL("WindowGeometries/VATSInfo"));
    textEdit->show();
}

void mvme::onObjectAboutToBeRemoved(QObject *object)
{
    auto &windowList = m_objectWindows[object];

    qDebug() << __PRETTY_FUNCTION__ << object << windowList;

    for (auto subwin: windowList)
        subwin->close();

    m_objectWindows.remove(object);
}

void mvme::appendToLog(const QString &str)
{
    auto debug(qDebug());
    debug.noquote();
    debug << __PRETTY_FUNCTION__ << str;

    if (m_logView)
    {
        m_logView->appendPlainText(str);
        auto bar = m_logView->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void mvme::updateWindowTitle()
{
    QString workspaceDir = m_context->getWorkspaceDirectory();
    workspaceDir.replace(QDir::homePath(), QSL("~"));

    QString title;
    switch (m_context->getMode())
    {
        case GlobalMode::DAQ:
            {
                title = QString("%1 - [DAQ mode] - mvme")
                    .arg(workspaceDir);
            } break;

        case GlobalMode::ListFile:
            {
                auto listFile = m_context->getReplayFile();
                QString fileName(QSL("<no listfile>"));
                if (listFile)
                {
                    QString filePath = listFile->getFileName();
                    fileName =  QFileInfo(filePath).fileName();
                }

                title = QString("%1 - %2 - [ListFile mode] - mvme")
                    .arg(workspaceDir)
                    .arg(fileName);
            } break;

        case GlobalMode::NotSet:
            break;
    }

    if (m_context->getMode() == GlobalMode::DAQ && m_context->isWorkspaceModified())
    {
        title += " *";
    }

    setWindowTitle(title);
}

void mvme::onConfigChanged(VMEConfig *config)
{
    connect(config, &VMEConfig::modifiedChanged, this, &mvme::updateWindowTitle);
    updateWindowTitle();
}

void mvme::clearLog()
{
    if (m_logView)
    {
        m_logView->clear();
    }
}

void mvme::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}

void mvme::onDAQAboutToStart(quint32 nCycles)
{
    QList<VMEScriptEditor *> scriptEditors;

    for (auto widgetList: m_objectWindows.values())
    {
        for (auto widget: widgetList)
        {
            if (auto scriptEditor = qobject_cast<VMEScriptEditor *>(widget))
            {
                if (scriptEditor->isModified())
                {
                    scriptEditors.push_back(scriptEditor);
                }
            }
        }
    }

    if (!scriptEditors.isEmpty())
    {
        QMessageBox box(QMessageBox::Question, QSL("Pending script modifications"),
                        QSL("Some VME scripts have been modified.\nDo you want to use those modifications for the current DAQ run?"),
                        QMessageBox::Yes | QMessageBox::No);

        int result = box.exec();

        if (result == QMessageBox::Yes)
        {
            for (auto scriptEditor: scriptEditors)
            {
                scriptEditor->applyChanges();
            }
        }
    }
}

void mvme::onDAQStateChanged(const DAQState &)
{
    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();

    {
        bool enable = true;

        if (globalMode == GlobalMode::DAQ && daqState != DAQState::Idle)
        {
            enable = false;
        }

        ui->actionOpenListfile->setEnabled(enable);
    }
}

void mvme::onShowDiagnostics(ModuleConfig *moduleConfig)
{
    if (m_context->getEventProcessor()->getDiagnostics())
        return;

    auto diag   = new MesytecDiagnostics;
    diag->setEventAndModuleIndices(m_context->getVMEConfig()->getEventAndModuleIndices(moduleConfig));
    auto eventProcessor = m_context->getEventProcessor();
    eventProcessor->setDiagnostics(diag);

    auto widget = new MesytecDiagnosticsWidget(diag);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    add_widget_close_action(widget);
    m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/MesytecDiagnostics"));

    connect(widget, &MVMEWidget::aboutToClose, this, [this]() {
        qDebug() << __PRETTY_FUNCTION__ << "diagnostics about to close";
        QMetaObject::invokeMethod(m_context->getEventProcessor(), "removeDiagnostics", Qt::QueuedConnection);
    });

    connect(m_context, &MVMEContext::daqStateChanged, widget, [this, widget] (const DAQState &state) {
        if (state == DAQState::Running)
        {
            widget->clearResultsDisplay();
        }

    });

    widget->show();
    widget->raise();
}

void mvme::on_actionImport_Histo1D_triggered()
{
    QSettings settings;
    QString path = settings.value(QSL("Files/LastHistogramExportDirectory")).toString();

    if (path.isEmpty())
    {
        path = m_context->getWorkspaceDirectory();
    }

    QString filename = QFileDialog::getOpenFileName(
        this, QSL("Import Histogram"),
        path,
        QSL("Histogram files (*.histo1d);; All Files (*)"));

    if (filename.isEmpty())
        return;

    QFile inFile(filename);

    if (inFile.open(QIODevice::ReadOnly))
    {
        QTextStream inStream(&inFile);

        auto histo = readHisto1D(inStream);

        if (histo)
        {
            auto widget = new Histo1DWidget(histo);
            widget->setAttribute(Qt::WA_DeleteOnClose);
            histo->setParent(widget);

            path = QFileInfo(filename).dir().path();

            if (path != m_context->getWorkspaceDirectory())
            {
                settings.setValue(QSL("Files/LastHistogramExportDirectory"), path);
            }
            else
            {
                settings.remove(QSL("Files/LastHistogramExportDirectory"));
            }

            addWidget(widget);
        }
    }
}

void mvme::on_actionVMEScriptRef_triggered()
{
    auto widgets = QApplication::topLevelWidgets();
    auto it = std::find_if(widgets.begin(), widgets.end(), [](const QWidget *widget) {
        return widget->objectName() == QSL("VMEScriptReference");
    });

    if (it != widgets.end())
    {
        auto widget = *it;
        widget->show();
        widget->showNormal();
        widget->raise();
    }
    else
    {
        auto widget = make_vme_script_ref_widget();
        addWidget(widget, widget->objectName());
    }
}

bool mvme::createNewOrOpenExistingWorkspace()
{
    do
    {
        auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
        auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Create a new or select an existing workspace directory"), startDir);

        if (dirName.isEmpty())
        {
            // Dialog was canceled
            return false;
        }

        QDir dir(dirName);

        bool triedToOpenExisting = true;

        try
        {
            if (dir.entryList(QDir::AllEntries | QDir::NoDot | QDir::NoDotDot).isEmpty())
            {
                triedToOpenExisting = false;
                m_context->newWorkspace(dirName);
            }
            else
            {
                triedToOpenExisting = true;
                m_context->openWorkspace(dirName);
            }
        } catch (const QString &e)
        {
            Q_ASSERT(!m_context->isWorkspaceOpen());

            QString title;
            if (triedToOpenExisting)
            {
                title = QSL("Error opening workspace");
            }
            else
            {
                title = QSL("Error creating workspace");
            }

            QMessageBox::warning(this, title, e);
        }
    } while (!m_context->isWorkspaceOpen());

    return true;
}

void mvme::updateActions()
{
    if (m_quitting) return;

    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();
    auto eventProcState = m_context->getEventProcessorState();
    auto controllerState = m_context->getController()->getState();

    bool isDAQIdle = (daqState == DAQState::Idle);

    // Workspaces
    ui->actionNewWorkspace->setEnabled(isDAQIdle);
    ui->actionOpenWorkspace->setEnabled(isDAQIdle);

    // VME Config
    ui->actionNewVMEConfig->setEnabled(isDAQIdle);
    ui->actionOpenVMEConfig->setEnabled(isDAQIdle);

    // Listfiles
    ui->actionOpenListfile->setEnabled(isDAQIdle);
    ui->actionCloseListfile->setEnabled(isDAQIdle);
}
