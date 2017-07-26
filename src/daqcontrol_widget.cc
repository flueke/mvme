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
#include "daqcontrol_widget.h"
#include "mvme_context.h"
#include "util.h"
#include "ui_daqcontrol_widget.h"

#include <QTimer>

static const int updateInterval = 500;

// zlib supports [0,9] with 6 being the default
static const int compressionMin = 0;
static const int compressionMax = 1;

static void fill_compression_combo(QComboBox *combo)
{
    for (int i = compressionMin;
         i <= compressionMax;
         ++i)
    {
        QString label(QString::number(i));

        switch (i)
        {
            case 0: label = QSL("No compression"); break;
            case 1: label = QSL("Fast compression"); break;
            case 6: label = QSL("Default"); break;
            case 9: label = QSL("Best compression"); break;
        }

        combo->addItem(label, i);
    }
}

DAQControlWidget::DAQControlWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DAQControlWidget)
    , m_context(context)
    , m_menuStartButton(new QMenu(this))
    , m_menuOneCycleButton(new QMenu(this))
{
    ui->setupUi(this);

    // start button actions
    auto do_start = [this](u32 nEvents, bool keepHistoContents)
    {
        if (m_context->getDAQState() == DAQState::Idle)
        {
            auto globalMode = m_context->getMode();

            if (globalMode == GlobalMode::DAQ)
            {
                m_context->startDAQ(nEvents, keepHistoContents);
            }
            else if (globalMode == GlobalMode::ListFile)
            {
                m_context->startReplay(nEvents, keepHistoContents);
            }
        }
    };

    auto actionStartKeepData  = m_menuStartButton->addAction(QSL("Keep histogram contents"));
    auto actionStartClearData = m_menuStartButton->addAction(QSL("Clear histogram contents"));

    ui->pb_start->setMenu(m_menuStartButton);

    connect(actionStartKeepData, &QAction::triggered, this, [this, do_start]() {
        do_start(0, true);
    });

    connect(actionStartClearData, &QAction::triggered, this, [this, do_start]() {
        do_start(0, false);
    });

    // start button click if no menu is set
    connect(ui->pb_start, &QPushButton::clicked, m_context, [this] {
        auto globalMode = m_context->getMode();
        auto daqState = m_context->getDAQState();

        if (globalMode == GlobalMode::DAQ)
        {
            if (daqState == DAQState::Running)
                m_context->pauseDAQ();
            else if (daqState == DAQState::Paused)
                m_context->resumeDAQ();
        }
        else if (globalMode == GlobalMode::ListFile)
        {
            if (daqState == DAQState::Running)
                m_context->pauseReplay();
            else if (daqState == DAQState::Paused)
                m_context->resumeReplay();
        }
    });

    // one cycle button
    auto actionOneCycleKeepData = m_menuOneCycleButton->addAction(QSL("Keep histogram contents"));
    auto actionOneCycleClearData = m_menuOneCycleButton->addAction(QSL("Clear histogram copntents"));

    ui->pb_oneCycle->setMenu(m_menuOneCycleButton);

    connect(actionOneCycleKeepData, &QAction::triggered, this, [this, do_start]() {
        do_start(1, true);
    });

    connect(actionOneCycleClearData, &QAction::triggered, this, [this, do_start]() {
        do_start(1, false);
    });

    // one cycle button click if no menu is set
    connect(ui->pb_oneCycle, &QPushButton::clicked, this, [this] {

        if (m_context->getMode() == GlobalMode::ListFile
            && m_context->getDAQState() == DAQState::Paused)
        {
            m_context->resumeReplay(1);
        }
    });

    connect(ui->pb_stop, &QPushButton::clicked, m_context, &MVMEContext::stopDAQ);

    connect(ui->pb_reconnect, &QPushButton::clicked, this, [this] {
        /* Just disconnect the controller here. MVMEContext will call
         * tryOpenController() periodically which will connect again. */
        auto ctrl = m_context->getController();
        if (ctrl)
            ctrl->close();
    });

    connect(ui->cb_writeListfile, &QCheckBox::stateChanged, this, [this](int state) {
        auto info = m_context->getListFileOutputInfo();
        info.enabled = (state != Qt::Unchecked);
        m_context->setListFileOutputInfo(info);
    });

    fill_compression_combo(ui->combo_compression);

    connect(ui->combo_compression, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged), this, [this] (int index) {
        int compression = ui->combo_compression->currentData().toInt();
        auto info = m_context->getListFileOutputInfo();
        info.compressionLevel = compression;
        m_context->setListFileOutputInfo(info);
    });

    connect(m_context, &MVMEContext::daqStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::eventProcessorStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::modeChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::controllerStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &DAQControlWidget::updateWidget);

    auto timer = new QTimer(this);
    timer->setInterval(updateInterval);
    timer->start();

    connect(timer, &QTimer::timeout, this, &DAQControlWidget::updateWidget);

    updateWidget();
}

DAQControlWidget::~DAQControlWidget()
{
    delete ui;
}

void DAQControlWidget::updateWidget()
{
    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();
    auto eventProcState = m_context->getEventProcessorState();
    auto controllerState = m_context->getController()->getState();
    const auto &stats = m_context->getDAQStats();

    //
    // start/pause/resume button
    //
    bool enableStartButton = false;

    if (globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened)
    {
        enableStartButton = true;
    }
    else if (globalMode == GlobalMode::ListFile) // && daqState == DAQState::Idle && eventProcState == EventProcessorState::Idle)
    {
        enableStartButton = true;
    }

    ui->pb_start->setEnabled(enableStartButton);

    //
    // stop button
    //
    ui->pb_stop->setEnabled(((globalMode == GlobalMode::DAQ && daqState != DAQState::Idle && controllerState == ControllerState::Opened)
                             || (globalMode == GlobalMode::ListFile && daqState != DAQState::Idle))
                           );

    //
    // one cycle button
    //
    bool enableOneCycleButton = false;

    if (globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened && daqState == DAQState::Idle)
    {
        enableOneCycleButton = true;
    }
    else if (globalMode == GlobalMode::ListFile && (daqState == DAQState::Idle || daqState == DAQState::Paused))
    {
        enableOneCycleButton = true;
    }

    ui->pb_oneCycle->setEnabled(enableOneCycleButton);


    //
    // listfile options
    //
    ui->gb_listfile->setEnabled(globalMode == GlobalMode::DAQ);

    //
    // button labels and actions
    //
    ui->pb_start->setMenu(nullptr);
    ui->pb_oneCycle->setMenu(nullptr);


    if (globalMode == GlobalMode::DAQ)
    {
        if (daqState == DAQState::Idle)
        {
            ui->pb_start->setText(QSL("Start"));
            ui->pb_start->setMenu(m_menuStartButton);
            ui->pb_oneCycle->setMenu(m_menuOneCycleButton);
        }
        else if (daqState == DAQState::Paused)
            ui->pb_start->setText(QSL("Resume"));
        else
            ui->pb_start->setText(QSL("Pause"));

        ui->pb_oneCycle->setText(QSL("1 Cycle"));
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        if (daqState == DAQState::Idle)
        {
            ui->pb_start->setText(QSL("Start Replay"));
            ui->pb_start->setMenu(m_menuStartButton);
            ui->pb_oneCycle->setMenu(m_menuOneCycleButton);
        }
        else if (daqState == DAQState::Paused)
            ui->pb_start->setText(QSL("Resume Replay"));
        else
            ui->pb_start->setText(QSL("Pause Replay"));

        if (daqState == DAQState::Idle)
            ui->pb_oneCycle->setText(QSL("1 Event"));
        else if (daqState == DAQState::Paused)
            ui->pb_oneCycle->setText(QSL("Next Event"));
    }


    auto daqStateString = DAQStateStrings.value(daqState, QSL("Unknown"));
    QString eventProcStateString = (eventProcState == EventProcessorState::Idle ? QSL("Idle") : QSL("Running"));

    if (daqState == DAQState::Running && globalMode == GlobalMode::ListFile)
        daqStateString = QSL("Replay");

    if (daqState != DAQState::Idle)
    {
        auto duration  = stats.startTime.secsTo(QDateTime::currentDateTime());
        auto durationString = makeDurationString(duration);

        daqStateString = QString("%1 (%2)").arg(daqStateString).arg(durationString);
    }


    ui->label_daqState->setText(daqStateString);
    ui->label_analysisState->setText(eventProcStateString);

    ui->label_controllerState->setText(controllerState == ControllerState::Closed
                                       ? QSL("Disconnected")
                                       : QSL("Connected"));

    ui->pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);

    auto outputInfo = m_context->getListFileOutputInfo();

    {
        QSignalBlocker b(ui->cb_writeListfile);
        ui->cb_writeListfile->setChecked(outputInfo.enabled);
    }

    {
        QSignalBlocker b(ui->combo_compression);
        ui->combo_compression->setCurrentIndex(outputInfo.compressionLevel);
    }

    auto filename = stats.listfileFilename;
    filename.remove(m_context->getWorkspacePath(QSL("ListFileDirectory")) + QSL("/"));

    if (ui->le_listfileFilename->text() != filename)
        ui->le_listfileFilename->setText(filename);


    QString sizeString;

    if (globalMode == GlobalMode::DAQ)
    {
        double mb = static_cast<double>(stats.listFileBytesWritten) / (1024.0*1024.0);
        sizeString = QString("%1 MB").arg(mb, 6, 'f', 2);
    }

    ui->label_listfileSize->setText(sizeString);
}
