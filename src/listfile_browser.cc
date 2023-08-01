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
#include "listfile_browser.h"

#include <QHeaderView>
#include <QBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <qnamespace.h>

#include "analysis/analysis.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme.h"

static const int PeriodicRefreshInterval_ms = 1000.0;

static const QStringList NameFilters = { QSL("*.mvmelst"), QSL("*.zip") };

ListfileBrowser::ListfileBrowser(MVMEContext *context, MVMEMainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_mainWindow(mainWindow)
    , m_fsModel(new QFileSystemModel(this))
    , m_fsView(new QTableView(this))
    , m_analysisLoadActionCombo(new QComboBox(this))
    , m_cb_replayAllParts(new QCheckBox(this))
{
    setWindowTitle(QSL("Listfile Browser"));

    set_widget_font_pointsize(this, 8);

    m_fsModel->setReadOnly(true);
    m_fsModel->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    m_fsModel->setNameFilters(NameFilters);
    m_fsModel->setNameFilterDisables(false);

    m_fsView->setModel(m_fsModel);
    m_fsView->verticalHeader()->hide();
    m_fsView->hideColumn(2); // Hides the file type column
    m_fsView->setSortingEnabled(true);

    auto widgetLayout = new QVBoxLayout(this);

    // On listfile load
    {
        m_analysisLoadActionCombo->addItem(QSL("keep current analysis"),        false);
        m_analysisLoadActionCombo->addItem(QSL("load analysis from listfile"),  true);

        m_cb_replayAllParts->setText("replay all parts");
        m_cb_replayAllParts->setChecked(true);

        auto layout = new QFormLayout;
        layout->addRow(QSL("On listfile load"), m_analysisLoadActionCombo);
        layout->addRow(QSL("Split Listfiles"),  m_cb_replayAllParts);

        widgetLayout->addLayout(layout);
    }

    widgetLayout->addWidget(m_fsView);

    connect(m_context, &MVMEContext::workspaceDirectoryChanged,
            this, [this](const QString &) { onWorkspacePathChanged(); });

    connect(m_context, &MVMEContext::daqStateChanged,
            this, &ListfileBrowser::onGlobalStateChanged);

    connect(m_context, &MVMEContext::modeChanged,
            this, &ListfileBrowser::onGlobalStateChanged);

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        m_fsView->resizeColumnsToContents();
        m_fsView->resizeRowsToContents();
    });

    connect(m_fsView, &QAbstractItemView::doubleClicked,
            this, &ListfileBrowser::onItemDoubleClicked);

    onWorkspacePathChanged();
    onGlobalStateChanged();
    m_fsView->horizontalHeader()->restoreState(QSettings().value("ListfileBrowser/HorizontalHeaderState").toByteArray());

    auto refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &ListfileBrowser::periodicUpdate);
    refreshTimer->setInterval(PeriodicRefreshInterval_ms);
    refreshTimer->start();
}

ListfileBrowser::~ListfileBrowser()
{
    QSettings().setValue("ListfileBrowser/HorizontalHeaderState", m_fsView->horizontalHeader()->saveState());
}

void ListfileBrowser::onWorkspacePathChanged()
{
    auto workspaceDirectory = m_context->getWorkspaceDirectory();
    auto workspaceSettings  = m_context->makeWorkspaceSettings();

    QDir dir(workspaceDirectory);
    QString listfileDirectory = dir.filePath(
        workspaceSettings->value(QSL("ListFileDirectory")).toString());

    m_fsModel->setRootPath(listfileDirectory);
    m_fsView->setRootIndex(m_fsModel->index(listfileDirectory));
}

void ListfileBrowser::onGlobalStateChanged()
{
    qDebug() << __PRETTY_FUNCTION__;
    auto globalMode = m_context->getMode();
    auto daqState   = m_context->getDAQState();

    bool disableBrowser = (globalMode == GlobalMode::DAQ && daqState != DAQState::Idle);

    m_fsView->setEnabled(!disableBrowser);
}

void ListfileBrowser::periodicUpdate()
{
    // FIXME: does not update file sizes reliably. calling reset() on the view
    // doesn't fix the size problem and also makes selections and stuff go
    // away, which is not desired at all.
    // Why is there no easy way to force a refresh for the current root index
    // of the view? Why is this stuff always difficult and never easy?
    auto rootPath = m_fsModel->rootPath();
    //qDebug() << __PRETTY_FUNCTION__ << "epicly failing to fail!!1111 rootPath=" << rootPath;
    m_fsModel->setRootPath(QSL(""));
    m_fsModel->setRootPath(rootPath);
    m_fsView->setRootIndex(m_fsModel->index(rootPath));
}

static const QString AnalysisFileFilter = QSL("MVME Analysis Files (*.analysis);; All Files (*.*)");

void ListfileBrowser::onItemDoubleClicked(const QModelIndex &mi)
{
    if (m_context->getMode() == GlobalMode::DAQ
        && m_context->getDAQState() != DAQState::Idle)
    {
        return;
    }

    if (!gui_vmeconfig_maybe_save_if_modified(m_context->getAnalysisServiceProvider()).first)
        return;

    OpenListfileOptions opts = {};

    opts.loadAnalysis = m_analysisLoadActionCombo->currentData().toBool();
    opts.replayAllParts = m_cb_replayAllParts->isChecked();

    if (opts.loadAnalysis && m_context->getAnalysis()->isModified())
    {
        if (!gui_analysis_maybe_save_if_modified(m_context->getAnalysisServiceProvider()).first)
            return;
    }

    auto filename = m_fsModel->filePath(mi);

    try
    {
        const auto &replayHandle = context_open_listfile(m_context, filename, opts);

        if (!replayHandle.messages.isEmpty())
        {
            m_context->logMessageRaw(QSL(">>>>> Begin listfile log"));
            m_context->logMessageRaw(replayHandle.messages);
            m_context->logMessageRaw(QSL("<<<<< End listfile log"));
        }
        m_mainWindow->updateWindowTitle();
    }
    catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Error opening listfile"),
                              QString("Error opening listfile %1: %2")
                              .arg(filename)
                              .arg(e));
    }
}
