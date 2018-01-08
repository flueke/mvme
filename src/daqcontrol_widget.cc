/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
    , pb_runSettings(new QPushButton)
    , label_controllerState(new QLabel)
    , label_daqState(new QLabel)
    , label_analysisState(new QLabel)
    , label_listfileSize(new QLabel)
    , cb_writeListfile(new QCheckBox)
    , combo_compression(new QComboBox)
    , le_listfileFilename(new QLineEdit)
    , gb_listfile(new QGroupBox)
    , gb_listfileLayout(nullptr)
    , rb_keepData(new QRadioButton("Keep"))
    , rb_clearData(new QRadioButton("Clear"))
    , bg_daqData(new QButtonGroup(this))
{
    bg_daqData->addButton(rb_keepData);
    bg_daqData->addButton(rb_clearData);
    rb_clearData->setChecked(true);

    // start button actions
    connect(pb_start, &QPushButton::clicked, m_context, [this] {
        auto globalMode = m_context->getMode();
        auto daqState = m_context->getDAQState();
        bool keepHistoContents = rb_keepData->isChecked();

        switch (daqState)
        {
            case DAQState::Idle:
                {
                    switch (globalMode)
                    {
                        case GlobalMode::DAQ:
                            m_context->startDAQReadout(0, keepHistoContents);
                            break;
                        case GlobalMode::ListFile:
                            m_context->startDAQReplay(0, keepHistoContents);
                            break;
                    }
                } break;

            case DAQState::Running:
                {
                    m_context->pauseDAQ();
                } break;

            case DAQState::Paused:
                {
                    m_context->resumeDAQ();
                } break;

            case DAQState::Starting:
            case DAQState::Stopping:
                break;
        }
    });

    // one cycle button
    connect(pb_oneCycle, &QPushButton::clicked, this, [this] {
        auto globalMode = m_context->getMode();
        auto daqState = m_context->getDAQState();
        bool keepHistoContents = rb_keepData->isChecked();

        switch (daqState)
        {
            case DAQState::Idle:
                {
                    switch (globalMode)
                    {
                        case GlobalMode::DAQ:
                            m_context->startDAQReadout(1, keepHistoContents);
                            break;
                        case GlobalMode::ListFile:
                            m_context->startDAQReplay(1, keepHistoContents);
                            break;
                    }
                } break;

            case DAQState::Running:
                {
                    m_context->pauseDAQ();
                } break;

            case DAQState::Paused:
                {
                    m_context->resumeDAQ(1);
                } break;

            case DAQState::Starting:
            case DAQState::Stopping:
                break;
        }
    });

    connect(pb_stop, &QPushButton::clicked, m_context, &MVMEContext::stopDAQ);

    connect(pb_reconnect, &QPushButton::clicked, this, [this] {
        /* Do not disconnect the controller directly but do it via the context.
         * This way the context can reset the connect-retry-count and attempt
         * to connect again. */

        m_context->reconnectVMEController();

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

    connect(pb_runSettings, &QPushButton::clicked, this, [this] {
        DAQRunSettingsDialog dialog(m_context->getListFileOutputInfo());
        dialog.setWindowModality(Qt::ApplicationModal);
        if (dialog.exec() == QDialog::Accepted)
        {
            m_context->setListFileOutputInfo(dialog.getSettings());
        }
    });

    fill_compression_combo(combo_compression);

    connect(combo_compression, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged), this, [this] (int index) {
        int compression = combo_compression->currentData().toInt();
        auto info = m_context->getListFileOutputInfo();
        info.compressionLevel = compression;
        m_context->setListFileOutputInfo(info);
    });

    connect(m_context, &MVMEContext::daqStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::mvmeStreamWorkerStateChanged, this, &DAQControlWidget::updateWidget);
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
    pb_runSettings->setText(QSL("Run Settings"));

    {
        auto pal = le_listfileFilename->palette();
        pal.setColor(QPalette::Base, QSL("#efebe7"));
        le_listfileFilename->setPalette(pal);
        le_listfileFilename->setReadOnly(true);
    }

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

    // keep/clear histo data radio buttons
    {
        auto rb_layout = new QHBoxLayout;
        rb_layout->setContentsMargins(0, 0, 0, 0);
        rb_layout->setSpacing(2);
        rb_layout->addWidget(rb_keepData);
        rb_layout->addWidget(rb_clearData);
        rb_layout->addStretch(1);
        stateFrameLayout->addRow(QSL("Histo data:"), rb_layout);
    }

    // vme controller
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
        gb_listfile->setTitle(QSL("Listfile Output:"));


        auto gbLayout = new QFormLayout(gb_listfile);
        gb_listfileLayout = gbLayout;
        gbLayout->setContentsMargins(0, 0, 0, 0);
        gbLayout->setSpacing(2);

        {
            cb_writeListfile->setText(QSL("Write Listfile"));
            auto hbox = new QHBoxLayout;
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(2);
            hbox->addWidget(cb_writeListfile);
            hbox->addWidget(new QLabel(QSL("Compression:")));
            hbox->addWidget(combo_compression);
            hbox->addStretch();
            gbLayout->addRow(hbox);
        }

        {
            auto hbox = new QHBoxLayout;
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(2);
            hbox->addWidget(pb_runSettings);
            hbox->addStretch();
            gbLayout->addRow(hbox);
        }

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
    auto streamWorkerState = m_context->getMVMEStreamWorkerState();
    auto controllerState = ControllerState::Disconnected;

    if (auto controller = m_context->getVMEController())
    {
        controllerState = m_context->getVMEController()->getState();
    }

    const bool isReplay  = (globalMode == GlobalMode::ListFile);
    const bool isRun     = (globalMode == GlobalMode::DAQ);
    const bool isDAQIdle = (daqState == DAQState::Idle);
    const bool isControllerConnected = (controllerState == ControllerState::Connected);


    const auto &stats = m_context->getDAQStats();

    //
    // start/pause/resume button
    //
    bool enableStartButton = false;

    if (globalMode == GlobalMode::DAQ && controllerState == ControllerState::Connected)
    {
        enableStartButton = true;
    }
    else if (globalMode == GlobalMode::ListFile) // && daqState == DAQState::Idle && streamWorkerState == MVMEStreamWorkerState::Idle)
    {
        enableStartButton = true;
    }

    pb_start->setEnabled(enableStartButton);

    //
    // stop button
    //
    pb_stop->setEnabled(((globalMode == GlobalMode::DAQ && daqState != DAQState::Idle && controllerState == ControllerState::Connected)
                             || (globalMode == GlobalMode::ListFile && daqState != DAQState::Idle))
                           );

    //
    // one cycle button
    //
    bool enableOneCycleButton = false;

    if (globalMode == GlobalMode::DAQ && controllerState == ControllerState::Connected && daqState == DAQState::Idle)
    {
        enableOneCycleButton = true;
    }
    else if (globalMode == GlobalMode::ListFile && (daqState == DAQState::Idle || daqState == DAQState::Paused))
    {
        enableOneCycleButton = true;
    }

    pb_oneCycle->setEnabled(enableOneCycleButton);


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

    if (daqState == DAQState::Running && globalMode == GlobalMode::ListFile)
        daqStateString = QSL("Replay");

    if (daqState != DAQState::Idle)
    {
        auto duration  = stats.startTime.secsTo(QDateTime::currentDateTime());
        auto durationString = makeDurationString(duration);

        daqStateString = QString("%1 (%2)").arg(daqStateString).arg(durationString);
    }

    label_daqState->setText(daqStateString);
    label_analysisState->setText(MVMEStreamWorkerState_StringTable.value(streamWorkerState, QSL("unknown")));

    rb_keepData->setEnabled(daqState == DAQState::Idle);
    rb_clearData->setEnabled(daqState == DAQState::Idle);

    QString stateString;

    switch (controllerState)
    {
        case ControllerState::Disconnected:
            stateString = QSL("Disconnected");
            break;

        case ControllerState::Connecting:
            stateString = QSL("Connecting");
            break;

        case ControllerState::Connected:
            stateString = QSL("Connected");
            break;
    }

    label_controllerState->setText(stateString);

    pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);
    pb_controllerSettings->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);

    //
    // listfile options
    //

    switch (globalMode)
    {
        case GlobalMode::DAQ:
            gb_listfile->setTitle(QSL("Listfile Output:"));
            break;

        case GlobalMode::ListFile:
            gb_listfile->setTitle(QSL("Listfile Info:"));
            break;
    };

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


    double mb = 0.0;

    auto sizeLabel = qobject_cast<QLabel *>(gb_listfileLayout->labelForField(label_listfileSize));

    switch (globalMode)
    {
        case GlobalMode::DAQ:
            // FIXME: use the actual size of the file on disk as compression is a thing...
            mb = static_cast<double>(stats.listFileBytesWritten) / (1024.0*1024.0);
            sizeLabel->setText(QSL("Current Size:"));
            break;

        case GlobalMode::ListFile:
            mb = static_cast<double>(stats.listFileTotalBytes) / (1024.0*1024.0);
            sizeLabel->setText(QSL("Replay Size:"));
            break;
    }

    auto sizeString = QString("%1 MB").arg(mb, 6, 'f', 2);

    label_listfileSize->setText(sizeString);

    cb_writeListfile->setEnabled(isDAQIdle && !isReplay);
    combo_compression->setEnabled(isDAQIdle && !isReplay);
    pb_runSettings->setEnabled(isDAQIdle && !isReplay);
}

DAQRunSettingsDialog::DAQRunSettingsDialog(const ListFileOutputInfo &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , le_prefix(new QLineEdit(this))
    , spin_runNumber(new QSpinBox(this))
    , cb_useRunNumber(new QCheckBox(this))
    , cb_useTimestamp(new QCheckBox(this))
    , le_exampleName(new QLineEdit(this))
    , m_bb(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(QSL("DAQ Run Settings"));
    setMinimumWidth(400);

    le_exampleName->setReadOnly(true);
    spin_runNumber->setMinimum(1);

    // populate
    le_prefix->setText(settings.prefix);
    spin_runNumber->setValue(settings.runNumber);
    cb_useRunNumber->setChecked(settings.flags & ListFileOutputInfo::UseRunNumber);
    cb_useTimestamp->setChecked(settings.flags & ListFileOutputInfo::UseTimestamp);

    connect(le_prefix, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_settings.prefix = text;
        updateExample();
    });

    connect(spin_runNumber, static_cast<void (QSpinBox::*)(int num)>(&QSpinBox::valueChanged),
            this, [this] (int num) {
                m_settings.runNumber = num;
                updateExample();
            });

    connect(cb_useRunNumber, &QCheckBox::stateChanged, this, [this](int) {
        if (cb_useRunNumber->isChecked())
        {
            m_settings.flags |= ListFileOutputInfo::UseRunNumber;
        }
        else
        {
            m_settings.flags &= ~ListFileOutputInfo::UseRunNumber;
        }
        updateExample();
    });

    connect(cb_useTimestamp, &QCheckBox::stateChanged, this, [this](int) {
        if (cb_useTimestamp->isChecked())
        {
            m_settings.flags |= ListFileOutputInfo::UseTimestamp;
        }
        else
        {
            m_settings.flags &= ~ListFileOutputInfo::UseTimestamp;
        }
        updateExample();
    });

    QObject::connect(m_bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto widgetLayout = new QFormLayout(this);

    widgetLayout->addRow(QSL("Prefix"), le_prefix);
    widgetLayout->addRow(QSL("Use Run Number"), cb_useRunNumber);
    widgetLayout->addRow(QSL("Next Run Number"), spin_runNumber);
    widgetLayout->addRow(QSL("Use Timestamp"), cb_useTimestamp);
    widgetLayout->addRow(make_separator_frame());
    widgetLayout->addRow(QSL("Example filename"), le_exampleName);
    widgetLayout->addRow(m_bb);

    updateExample();
}

void DAQRunSettingsDialog::updateExample()
{
    le_exampleName->setText(generate_output_filename(m_settings));
}
