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

#include <QHBoxLayout>
#include <QFormLayout>
#include <QTimer>

#include "mvme_context.h"
#include "util.h"
#include "vme_controller_ui.h"

static const int updateInterval = 500;

// zlib supports [0,9] with 6 being the default.
//
// Sample data from an MDPP-16 showed that compression levels > 1 lead to very
// minor improvements in compressed file size but cause major performance
// decreases. To not throttle the DAQ with compression we're now limiting the
// selectable compression levels to 0 (no compression) and 1.
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
    , m_context(context)
    , pb_start(new QPushButton)
    , pb_stop(new QPushButton)
    , pb_oneCycle(new QPushButton)
    , pb_reconnect(new QPushButton)
    , pb_controllerSettings(new QPushButton)
    , menu_startButton(new QMenu(this))
    , menu_oneCycleButton(new QMenu(this))
    , label_controllerState(new QLabel)
    , label_daqState(new QLabel)
    , label_analysisState(new QLabel)
    , label_listfileSize(new QLabel)
    , cb_writeListfile(new QCheckBox)
    , combo_compression(new QComboBox)
    , le_listfileFilename(new QLineEdit)
    , gb_listfile(new QGroupBox)
{
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

    auto actionStartKeepData  = menu_startButton->addAction(QSL("Keep histogram contents"));
    auto actionStartClearData = menu_startButton->addAction(QSL("Clear histogram contents"));

    pb_start->setMenu(menu_startButton);

    connect(actionStartKeepData, &QAction::triggered, this, [this, do_start]() {
        do_start(0, true);
    });

    connect(actionStartClearData, &QAction::triggered, this, [this, do_start]() {
        do_start(0, false);
    });

    // start button click if no menu is set
    connect(pb_start, &QPushButton::clicked, m_context, [this] {
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
    auto actionOneCycleKeepData = menu_oneCycleButton->addAction(QSL("Keep histogram contents"));
    auto actionOneCycleClearData = menu_oneCycleButton->addAction(QSL("Clear histogram contents"));

    pb_oneCycle->setMenu(menu_oneCycleButton);

    connect(actionOneCycleKeepData, &QAction::triggered, this, [this, do_start]() {
        do_start(1, true);
    });

    connect(actionOneCycleClearData, &QAction::triggered, this, [this, do_start]() {
        do_start(1, false);
    });

    // one cycle button click if no menu is set
    connect(pb_oneCycle, &QPushButton::clicked, this, [this] {

        if (m_context->getMode() == GlobalMode::ListFile
            && m_context->getDAQState() == DAQState::Paused)
        {
            m_context->resumeReplay(1);
        }
    });

    connect(pb_stop, &QPushButton::clicked, m_context, &MVMEContext::stopDAQ);

    connect(pb_reconnect, &QPushButton::clicked, this, [this] {
        /* Just disconnect the controller here. MVMEContext will call
         * tryOpenController() periodically which will connect again. */
        auto ctrl = m_context->getVMEController();
        if (ctrl)
            ctrl->close();
    });

    connect(pb_controllerSettings, &QPushButton::clicked, this, [this] {
        VMEControllerSettingsDialog dialog(m_context);
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.exec();
    });

    connect(cb_writeListfile, &QCheckBox::stateChanged, this, [this](int state) {
        auto info = m_context->getListFileOutputInfo();
        info.enabled = (state != Qt::Unchecked);
        m_context->setListFileOutputInfo(info);
    });

    fill_compression_combo(combo_compression);

    connect(combo_compression, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged), this, [this] (int index) {
        int compression = combo_compression->currentData().toInt();
        auto info = m_context->getListFileOutputInfo();
        info.compressionLevel = compression;
        m_context->setListFileOutputInfo(info);
    });

    connect(m_context, &MVMEContext::daqStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::eventProcessorStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::modeChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::controllerStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &DAQControlWidget::updateWidget);


    //
    // layout
    //

    pb_start->setText(QSL("Start"));
    pb_stop->setText(QSL("Stop"));
    pb_oneCycle->setText(QSL("1 Cycle"));
    pb_reconnect->setText(QSL("Reconnect"));
    pb_controllerSettings->setText(QSL("Settings"));

    {
        auto pal = le_listfileFilename->palette();
        pal.setColor(QPalette::Base, QSL("#efebe7"));
        le_listfileFilename->setPalette(pal);
        le_listfileFilename->setReadOnly(true);
    }

    gb_listfile->setTitle(QSL("Listfile Output:"));

    auto daqButtonFrame = new QFrame;
    auto daqButtonLayout = new QHBoxLayout(daqButtonFrame);
    daqButtonLayout->setContentsMargins(0, 0, 0, 0);
    daqButtonLayout->setSpacing(2);
    daqButtonLayout->addWidget(pb_start);
    daqButtonLayout->addWidget(pb_stop);
    daqButtonLayout->addWidget(pb_oneCycle);

    // state frame
    auto stateFrame = new QFrame;
    auto stateFrameLayout = new QFormLayout(stateFrame);
    stateFrameLayout->setContentsMargins(0, 0, 0, 0);
    stateFrameLayout->setSpacing(2);

    {
        auto ctrlLayout = new QHBoxLayout;
        ctrlLayout->setContentsMargins(0, 0, 0, 0);
        ctrlLayout->setSpacing(2);
        ctrlLayout->addWidget(label_controllerState);
        ctrlLayout->addWidget(pb_reconnect);
        ctrlLayout->addWidget(pb_controllerSettings);
        stateFrameLayout->addRow(QSL("VME Controller:"), ctrlLayout);
    }

    stateFrameLayout->addRow(QSL("DAQ State:"), label_daqState);
    stateFrameLayout->addRow(QSL("Analysis State:"), label_analysisState);

    // listfile groupbox
    {
        cb_writeListfile->setText(QSL("Write Listfile"));
        auto hbox = new QHBoxLayout;
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(2);
        hbox->addWidget(cb_writeListfile);
        hbox->addWidget(new QLabel(QSL("Compression:")));
        hbox->addWidget(combo_compression);
        hbox->addStretch();

        auto gbLayout = new QFormLayout(gb_listfile);
        gbLayout->setContentsMargins(0, 0, 0, 0);
        gbLayout->setSpacing(2);
        gbLayout->addRow(hbox);
        gbLayout->addRow(QSL("Current Filename:"), le_listfileFilename);
        gbLayout->addRow(QSL("Current Size:"), label_listfileSize);
    }

    // widget layout
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(2);
    widgetLayout->addWidget(daqButtonFrame);
    widgetLayout->addWidget(stateFrame);
    widgetLayout->addWidget(gb_listfile);

    //
    // widget update timer setup
    //
    auto timer = new QTimer(this);
    timer->setInterval(updateInterval);
    timer->start();

    connect(timer, &QTimer::timeout, this, &DAQControlWidget::updateWidget);

    updateWidget();
}

DAQControlWidget::~DAQControlWidget()
{
}

void DAQControlWidget::updateWidget()
{
    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();
    auto eventProcState = m_context->getEventProcessorState();
    auto controllerState = ControllerState::Unknown;

    if (auto controller = m_context->getVMEController())
    {
        controllerState = m_context->getVMEController()->getState();
    }

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

    pb_start->setEnabled(enableStartButton);

    //
    // stop button
    //
    pb_stop->setEnabled(((globalMode == GlobalMode::DAQ && daqState != DAQState::Idle && controllerState == ControllerState::Opened)
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

    pb_oneCycle->setEnabled(enableOneCycleButton);


    //
    // listfile options
    //
    gb_listfile->setEnabled(globalMode == GlobalMode::DAQ);

    //
    // button labels and actions
    //
    pb_start->setMenu(nullptr);
    pb_oneCycle->setMenu(nullptr);


    if (globalMode == GlobalMode::DAQ)
    {
        if (daqState == DAQState::Idle)
        {
            pb_start->setText(QSL("Start"));
            pb_start->setMenu(menu_startButton);
            pb_oneCycle->setMenu(menu_oneCycleButton);
        }
        else if (daqState == DAQState::Paused)
            pb_start->setText(QSL("Resume"));
        else
            pb_start->setText(QSL("Pause"));

        pb_oneCycle->setText(QSL("1 Cycle"));
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        if (daqState == DAQState::Idle)
        {
            pb_start->setText(QSL("Start Replay"));
            pb_start->setMenu(menu_startButton);
            pb_oneCycle->setMenu(menu_oneCycleButton);
        }
        else if (daqState == DAQState::Paused)
            pb_start->setText(QSL("Resume Replay"));
        else
            pb_start->setText(QSL("Pause Replay"));

        if (daqState == DAQState::Idle)
            pb_oneCycle->setText(QSL("1 Event"));
        else if (daqState == DAQState::Paused)
            pb_oneCycle->setText(QSL("Next Event"));
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


    label_daqState->setText(daqStateString);
    label_analysisState->setText(eventProcStateString);

    label_controllerState->setText(controllerState == ControllerState::Opened
                                       ? QSL("Connected")
                                       : QSL("Disconnected"));

    pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);
    pb_controllerSettings->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);

    auto outputInfo = m_context->getListFileOutputInfo();

    {
        QSignalBlocker b(cb_writeListfile);
        cb_writeListfile->setChecked(outputInfo.enabled);
    }

    {
        QSignalBlocker b(combo_compression);
        combo_compression->setCurrentIndex(outputInfo.compressionLevel);
    }

    auto filename = stats.listfileFilename;
    filename.remove(m_context->getWorkspacePath(QSL("ListFileDirectory")) + QSL("/"));

    if (le_listfileFilename->text() != filename)
        le_listfileFilename->setText(filename);


    QString sizeString;

    if (globalMode == GlobalMode::DAQ)
    {
        double mb = static_cast<double>(stats.listFileBytesWritten) / (1024.0*1024.0);
        sizeString = QString("%1 MB").arg(mb, 6, 'f', 2);
    }

    label_listfileSize->setText(sizeString);
}
