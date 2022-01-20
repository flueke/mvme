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
#include "daqcontrol_widget.h"

#include <QHBoxLayout>
#include <QFormLayout>
#include <QRegularExpressionValidator>
#include <QStorageInfo>

#include "mvme_context.h"
#include "mvme_workspace.h"
#include "qt_util.h"
#include "sis3153.h"
#include "util.h"
#include "util/strings.h"
#include "vme_controller_ui.h"

#if 0
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
#else
enum CompressionPreset
{
    NoCompression = 0,  // ZIP, level 0 aka store
    Fast_LZ4 = 1,       // LZ4, level 0 (faster than ZIP level 1)
    Fast_ZIP = 2,       // ZIP, level 1 aka "super fast"
};

static void fill_compression_combo(QComboBox *combo, bool isMVLC)
{
    QSignalBlocker blocker(combo);
    combo->clear();

    combo->addItem(QSL("No compression"), CompressionPreset::NoCompression);

    if (isMVLC)
        combo->addItem(QSL("LZ4 fast"), CompressionPreset::Fast_LZ4);

    combo->addItem(QSL("ZIP fast"), CompressionPreset::Fast_ZIP);
}
#endif

DAQControlWidget::DAQControlWidget(QWidget *parent)
    : QWidget(parent)
    , pb_start(new QPushButton)
    , pb_stop(new QPushButton)
    , pb_oneCycle(new QPushButton)
    , pb_reconnect(new QPushButton)
    , pb_controllerSettings(new QPushButton)
    , pb_runSettings(new QPushButton)
    , pb_workspaceSettings(new QPushButton)
    , pb_runNotes(new QPushButton)
    , pb_forceReset(new QPushButton)
    , label_controllerState(new QLabel)
    , label_daqState(new QLabel)
    , label_analysisState(new QLabel)
    , label_listfileSize(new QLabel)
    , label_freeStorageSpace(new QLabel)
    , cb_writeListfile(new QCheckBox)
    , combo_compression(new QComboBox)
    , le_listfileFilename(new QLineEdit)
    , gb_listfile(new QGroupBox)
    , gb_listfileLayout(nullptr)
    , rb_keepData(new QRadioButton("Keep"))
    , rb_clearData(new QRadioButton("Clear"))
    , bg_daqData(new QButtonGroup(this))
    , spin_runDuration(new QSpinBox(this))
{
    bg_daqData->addButton(rb_keepData);
    bg_daqData->addButton(rb_clearData);
    rb_clearData->setChecked(true);
    fill_compression_combo(combo_compression, is_mvlc_controller(m_vmeControllerTypeName));

    auto daq_ctrl = [this] (u32 cycles)
    {
        switch (m_daqState)
        {
            case DAQState::Idle:
                {
                    bool keepHistoContents = rb_keepData->isChecked();
                    std::chrono::seconds runDuration(spin_runDuration->value());
                    emit startDAQ(cycles, keepHistoContents, runDuration);
                }
                break;

            case DAQState::Running:
                emit pauseDAQ();
                break;

            case DAQState::Paused:
                emit resumeDAQ(cycles);
                break;

            case DAQState::Starting:
            case DAQState::Stopping:
                break;
        }
    };

    // start button actions
    connect(pb_start, &QPushButton::clicked, this, [daq_ctrl] ()
    {
        daq_ctrl(0);
    });

    // one cycle button
    connect(pb_oneCycle, &QPushButton::clicked, this, [daq_ctrl] ()
    {
        daq_ctrl(1);
    });

    connect(pb_stop, &QPushButton::clicked,
            this, &DAQControlWidget::stopDAQ);

    connect(pb_reconnect, &QPushButton::clicked,
            this, &DAQControlWidget::reconnectVMEController);

    connect(pb_forceReset, &QPushButton::clicked,
            this, &DAQControlWidget::forceResetVMEController);

    connect(pb_controllerSettings, &QPushButton::clicked,
            this, &DAQControlWidget::changeVMEControllerSettings);

    connect(pb_runSettings, &QPushButton::clicked,
            this, &DAQControlWidget::changeDAQRunSettings);

    connect(pb_workspaceSettings, &QPushButton::clicked,
            this, &DAQControlWidget::changeWorkspaceSettings);

    connect(pb_runNotes, &QPushButton::clicked,
            this, &DAQControlWidget::showRunNotes);

    connect(cb_writeListfile, &QCheckBox::stateChanged,
            this, [this](int state)
    {
        m_listFileOutputInfo.enabled = (state != Qt::Unchecked);
        emit listFileOutputInfoModified(m_listFileOutputInfo);
    });

    connect(combo_compression, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int /*index*/)
    {
        auto preset = combo_compression->currentData().toInt();

        switch (preset)
        {
            case CompressionPreset::NoCompression:
                m_listFileOutputInfo.format = ListFileFormat::ZIP;
                m_listFileOutputInfo.compressionLevel = 0;
                break;

            case CompressionPreset::Fast_ZIP:
                m_listFileOutputInfo.format = ListFileFormat::ZIP;
                m_listFileOutputInfo.compressionLevel = 1;
                break;

            case CompressionPreset::Fast_LZ4:
                m_listFileOutputInfo.format = ListFileFormat::LZ4;
                m_listFileOutputInfo.compressionLevel = 0;
                break;

            default:
                return;
        }

        emit listFileOutputInfoModified(m_listFileOutputInfo);
    });



    //
    // layout
    //

    pb_start->setText(QSL("Start"));
    pb_stop->setText(QSL("Stop"));
    pb_oneCycle->setText(QSL("1 Cycle"));
    pb_reconnect->setText(QSL("Reconnect"));
    pb_forceReset->setText(QSL("Force Reset"));
    pb_controllerSettings->setText(QSL("Settings"));
    pb_runSettings->setText(QSL("Run Settings"));
    pb_workspaceSettings->setText(QSL("Workspace Settings"));
    pb_runNotes->setIcon(QIcon(QSL(":/text-document.png")));
    pb_runNotes->setText(QSL("Run Notes"));

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

    // run duration
    {
        spin_runDuration->setMinimum(0);
        spin_runDuration->setMaximum(std::numeric_limits<int>::max());
        spin_runDuration->setSpecialValueText(QSL("unlimited"));
        spin_runDuration->setSuffix(QSL(" s"));
        auto l = make_hbox<0, 2>();
        l->addWidget(spin_runDuration);
        l->addStretch(1);
        stateFrameLayout->addRow(QSL("Run duration:"), l);
    }

    // vme controller
    {
        auto ctrlLayout = new QGridLayout;
        ctrlLayout->setContentsMargins(0, 0, 0, 0);
        ctrlLayout->setSpacing(2);
        ctrlLayout->addWidget(label_controllerState, 0, 0);
        ctrlLayout->addWidget(pb_controllerSettings, 0, 1);
        ctrlLayout->addWidget(pb_reconnect, 1, 0);
        ctrlLayout->addWidget(pb_forceReset, 1, 1);

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
            hbox->addWidget(pb_workspaceSettings);
            hbox->addWidget(pb_runNotes);
            hbox->addStretch();
            gbLayout->addRow(hbox);
        }

        gbLayout->addRow(QSL("Current Filename:"), le_listfileFilename);
        gbLayout->addRow(QSL("Current Size:"), label_listfileSize);
        gbLayout->addRow(QSL("Free Space:"), label_freeStorageSpace);
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
    updateWidget();
}

DAQControlWidget::~DAQControlWidget()
{
}

void DAQControlWidget::setGlobalMode(const GlobalMode &mode)
{
    m_globalMode = mode;
    updateWidget();
}

void DAQControlWidget::setDAQState(const DAQState &state)
{
    m_daqState = state;
    updateWidget();
}

void DAQControlWidget::setVMEControllerState(const ControllerState &state)
{
    m_vmeControllerState = state;
    updateWidget();
}

void DAQControlWidget::setVMEControllerTypeName(const QString &name)
{
    m_vmeControllerTypeName = name;
    fill_compression_combo(combo_compression, is_mvlc_controller(m_vmeControllerTypeName));
    updateWidget();
}

void DAQControlWidget::setStreamWorkerState(const AnalysisWorkerState &state)
{
    m_streamWorkerState = state;
    updateWidget();
}

void DAQControlWidget::setListFileOutputInfo(const ListFileOutputInfo &info)
{
    m_listFileOutputInfo = info;
    updateWidget();
}

void DAQControlWidget::setDAQStats(const DAQStats &stats)
{
    m_daqStats = stats;
    // no forced update
}

void DAQControlWidget::setWorkspaceDirectory(const QString &dir)
{
    m_workspaceDirectory = dir;
    // no forced update
}

void DAQControlWidget::setMVMEState(const MVMEState &state)
{
    m_mvmeState = state;
    updateWidget();
}

void DAQControlWidget::updateWidget()
{
    auto globalMode = m_globalMode;
    auto daqState = m_daqState;
    auto streamWorkerState = m_streamWorkerState;
    auto controllerState = m_vmeControllerState;
    bool isMVLC = is_mvlc_controller(m_vmeControllerTypeName);

    const bool isReplay  = (globalMode == GlobalMode::ListFile);
    const bool isDAQIdle = (daqState == DAQState::Idle);
    const auto &stats = m_daqStats;

    //
    // start/pause/resume button
    //
    bool enableStartButton = false;

    if (globalMode == GlobalMode::DAQ && controllerState == ControllerState::Connected)
    {
        switch (m_mvmeState)
        {
            case MVMEState::Idle:
            case MVMEState::Running:
                enableStartButton = true;
                break;

            default:
                enableStartButton = false;
                break;
        }
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        enableStartButton = (
            m_mvmeState == MVMEState::Idle
            || m_mvmeState == MVMEState::Running);
    }

    pb_start->setEnabled(enableStartButton);

    //
    // stop button
    //

    bool canStopDAQ = (m_mvmeState == MVMEState::Running);


    pb_stop->setEnabled(((globalMode == GlobalMode::DAQ
                          && canStopDAQ
                          && controllerState == ControllerState::Connected)
                         || (globalMode == GlobalMode::ListFile && canStopDAQ)
                        ));

    //
    // one cycle button
    //
    {
        bool enableOneCycleButton = false;
        bool showOneCycleButton = !isMVLC;

        if (globalMode == GlobalMode::DAQ
            && controllerState == ControllerState::Connected
            && daqState == DAQState::Idle)
        {
            enableOneCycleButton = true;
        }
        else if (globalMode == GlobalMode::ListFile
                 && (daqState == DAQState::Idle || daqState == DAQState::Paused))
        {
            enableOneCycleButton = true;
        }

        pb_oneCycle->setEnabled(enableOneCycleButton);
        pb_oneCycle->setVisible(showOneCycleButton);
    }

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
    label_analysisState->setText(to_string(streamWorkerState));

    rb_keepData->setEnabled(daqState == DAQState::Idle);
    rb_clearData->setEnabled(daqState == DAQState::Idle);
    spin_runDuration->setEnabled(daqState == DAQState::Idle && globalMode == GlobalMode::DAQ);

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

    if (!m_vmeControllerTypeName.isEmpty())
    {
        stateString += " (" + m_vmeControllerTypeName + ")";
    }

    label_controllerState->setText(stateString);

    pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);
    pb_controllerSettings->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);
    pb_forceReset->setEnabled(controllerState == ControllerState::Disconnected);

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

    const auto &outputInfo = m_listFileOutputInfo;

    {
        QSignalBlocker b(cb_writeListfile);
        cb_writeListfile->setChecked(outputInfo.enabled);
    }

    {
        int comboData = -1;

        if (outputInfo.format == ListFileFormat::ZIP)
        {
            if (outputInfo.compressionLevel == 0)
                comboData = 0;
            else if (outputInfo.compressionLevel == 1)
                comboData = 2;
        }
        else if (outputInfo.format == ListFileFormat::LZ4)
            comboData = 1;

        for (int i=0; i<combo_compression->count(); ++i)
        {
            if (combo_compression->itemData(i).toInt() == comboData
                && combo_compression->currentIndex() != i)
            {
                QSignalBlocker b(combo_compression);
                qDebug() << __PRETTY_FUNCTION__ << "setting combo_compression index to" << i;
                combo_compression->setCurrentIndex(i);
                break;
            }
        }
    }

    auto filename = stats.listfileFilename;

    //qDebug() << __PRETTY_FUNCTION__ << "filename=" << filename;

    if (auto settings = make_workspace_settings(m_workspaceDirectory))
    {
        QDir listfileDir(settings->value(QSL("ListFileDirectory")).toString());

        if (!listfileDir.isAbsolute())
        {
            QString prefix = m_workspaceDirectory + "/" + listfileDir.path() + "/";

            if (filename.startsWith(prefix))
            {
                filename.remove(prefix);
            }
        }
    }

    if (le_listfileFilename->text() != filename)
        le_listfileFilename->setText(filename);


    auto sizeLabel = qobject_cast<QLabel *>(gb_listfileLayout->labelForField(label_listfileSize));

    switch (globalMode)
    {
        case GlobalMode::DAQ:
            {
                sizeLabel->setText(QSL("Current Size:"));
                QFile fi(stats.listfileFilename);
                auto str = format_number(fi.size(), QSL("B"), UnitScaling::Binary,
                                         // fieldWidth, format, precision
                                         0, 'f', 2);
                label_listfileSize->setText(str);
            } break;

        case GlobalMode::ListFile:
            {
                sizeLabel->setText(QSL("Replay Size:"));
                QFile fi(stats.listfileFilename);
                auto str = format_number(fi.size(), QSL("B"), UnitScaling::Binary,
                                         // fieldWidth, format, precision
                                         0, 'f', 2);
                label_listfileSize->setText(str);
            } break;
    }

    {
        QStorageInfo si(m_workspaceDirectory);
        auto freeBytes = si.bytesFree();
        auto str = format_number(freeBytes, QSL("B"), UnitScaling::Binary,
                                 // fieldWidth, format, precision
                                 0, 'f', 2);

        label_freeStorageSpace->setText(str);
    }

    cb_writeListfile->setEnabled(isDAQIdle && !isReplay);
    combo_compression->setEnabled(isDAQIdle && !isReplay);
    pb_runSettings->setEnabled(isDAQIdle && !isReplay);
    pb_workspaceSettings->setEnabled(isDAQIdle);
}

DAQRunSettingsDialog::DAQRunSettingsDialog(const ListFileOutputInfo &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , le_prefix(new QLineEdit(this))
    , spin_runNumber(new QSpinBox(this))
    , cb_useRunNumber(new QCheckBox(this))
    , cb_useTimestamp(new QCheckBox(this))
    , le_exampleName(new QLineEdit(this))
    , rb_dontSplit(new QRadioButton("Don't split", this))
    , rb_splitBySize(new QRadioButton("Split by size", this))
    , rb_splitByTime(new QRadioButton("Split by time", this))
    , spin_splitSize(new QSpinBox(this))
    , spin_splitTime(new QSpinBox(this))
    , m_bb(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(QSL("DAQ Run Settings"));
    setMinimumWidth(400);

    le_exampleName->setReadOnly(true);
    spin_runNumber->setMinimum(1);

    auto re_prefix = QRegularExpression(QSL("^[^\\\\/]+$"));
    le_prefix->setValidator(new QRegularExpressionValidator(re_prefix, le_prefix));

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

    spin_splitSize->setPrefix("split every ");
    spin_splitSize->setSuffix(" MB");
    spin_splitSize->setMinimum(1);
    spin_splitSize->setMaximum(std::numeric_limits<int>::max());
    spin_splitSize->setValue(settings.splitSize / Megabytes(1));

    spin_splitTime->setPrefix("split after ");
    spin_splitTime->setSuffix(" seconds");
    spin_splitTime->setMinimum(1);
    spin_splitTime->setMaximum(std::numeric_limits<int>::max());
    spin_splitTime->setValue(settings.splitTime.count());

    if (settings.flags & ListFileOutputInfo::SplitBySize)
        rb_splitBySize->setChecked(true);
    else if (settings.flags & ListFileOutputInfo::SplitByTime)
        rb_splitByTime->setChecked(true);
    else
        rb_dontSplit->setChecked(true);

    connect(spin_splitSize, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] (int value) {
                m_settings.splitSize = static_cast<size_t>(value) * Megabytes(1);
            });

    connect(spin_splitTime, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] (int value) {
                m_settings.splitTime = std::chrono::seconds(value);
            });

    connect(rb_dontSplit, &QRadioButton::clicked,
            this, [this] (bool checked) {
                if (checked)
                {
                    m_settings.flags &= ~(ListFileOutputInfo::SplitBySize | ListFileOutputInfo::SplitByTime);
                    updateExample();
                }
            });

    connect(rb_splitByTime, &QRadioButton::clicked,
            this, [this] (bool checked) {
                if (checked)
                {
                    m_settings.flags &= ~(ListFileOutputInfo::SplitBySize | ListFileOutputInfo::SplitByTime);
                    m_settings.flags |= ListFileOutputInfo::SplitByTime;
                    updateExample();
                }
            });

    connect(rb_splitBySize, &QRadioButton::clicked,
            this, [this] (bool checked) {
                if (checked)
                {
                    m_settings.flags &= ~(ListFileOutputInfo::SplitBySize | ListFileOutputInfo::SplitByTime);
                    m_settings.flags |= ListFileOutputInfo::SplitBySize;
                    updateExample();
                }
            });

    QObject::connect(m_bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto gb_listfileName = new QGroupBox("Listfile filename");
    auto l_listfileName = new QFormLayout(gb_listfileName);
    l_listfileName->addRow(QSL("Prefix"), le_prefix);
    l_listfileName->addRow(QSL("Use Run Number"), cb_useRunNumber);
    l_listfileName->addRow(QSL("Next Run Number"), spin_runNumber);
    l_listfileName->addRow(QSL("Use Timestamp"), cb_useTimestamp);

    auto gb_splitting = new QGroupBox("Listfile splitting");
    auto l_splitting = new QGridLayout(gb_splitting);
    l_splitting->addWidget(rb_dontSplit, 0, 0);
    l_splitting->addWidget(rb_splitBySize, 1, 0);
    l_splitting->addWidget(spin_splitSize, 1, 1);
    l_splitting->addWidget(rb_splitByTime, 2, 0);
    l_splitting->addWidget(spin_splitTime, 2, 1);

    auto l_exampleName = make_hbox();
    l_exampleName->addWidget(new QLabel("Example filename"));
    l_exampleName->addWidget(le_exampleName);

    auto widgetLayout = make_vbox(this);
    widgetLayout->addWidget(gb_listfileName);
    widgetLayout->addWidget(gb_splitting);
    widgetLayout->addWidget(make_separator_frame());
    widgetLayout->addLayout(l_exampleName);
    widgetLayout->addWidget(m_bb);

    updateExample();
}

DAQRunSettingsDialog::~DAQRunSettingsDialog()
{
}

void DAQRunSettingsDialog::updateExample()
{
    auto filename = generate_output_filename(m_settings);
    auto basename = QFileInfo(filename).completeBaseName();
    auto extension = QFileInfo(filename).completeSuffix();

    if (m_settings.flags & (ListFileOutputInfo::SplitByTime | ListFileOutputInfo::SplitBySize))
        filename = basename + "_part007." + extension;

    le_exampleName->setText(filename);
}

WorkspaceSettingsDialog::WorkspaceSettingsDialog(const std::shared_ptr<QSettings> &settings,
                                                 QWidget *parent)
    : QDialog(parent)
    , gb_jsonRPC(new QGroupBox(QSL("Enable JSON-RPC Server")))
    , gb_eventServer(new QGroupBox(QSL("Enable Event Server")))
    , le_jsonRPCListenAddress(new QLineEdit)
    , le_eventServerListenAddress(new QLineEdit)
    , spin_jsonRPCListenPort(new QSpinBox)
    , spin_eventServerListenPort(new QSpinBox)
    , cb_ignoreStartupErrors(new QCheckBox("Ignore VME Init Startup Errors"))
    , m_bb(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_settings(settings)
{
    auto widgetLayout = new QVBoxLayout(this);

    // Groupbox ExperimentName and ExperimentTitle
    {
        auto gb = new QGroupBox(QSL("Experiment"));
        le_expName = new QLineEdit(this);
        le_expTitle = new QLineEdit(this);
        auto l = new QFormLayout(gb);
        l->addRow(QSL("Experiment Name"), le_expName);
        l->addRow(QSL("Experiment Title"), le_expTitle);
        l->addRow(cb_ignoreStartupErrors);

        widgetLayout->addWidget(gb);
    }

    // JSONRPC
    gb_jsonRPC->setCheckable(true);
    spin_jsonRPCListenPort->setMinimum(1);
    spin_jsonRPCListenPort->setMaximum((1 << 16) - 1);

    {
        auto label = new QLabel(QSL(
                "Enables a built-in JSON-RPC server allowing remote control"
                " and remote status queries.\n"
                "The listen address may be a hostname or an IP address. Leave blank to"
                " bind to all local interfaces."));

        label->setWordWrap(true);
        set_widget_font_pointsize_relative(label, -1);

        auto l = new QFormLayout(gb_jsonRPC);
        l->addRow(label);
        l->addRow(QSL("Listen Address"), le_jsonRPCListenAddress);
        l->addRow(QSL("Listen Port"), spin_jsonRPCListenPort);
    }

    // Event Server
    gb_eventServer->setCheckable(true);
    spin_eventServerListenPort->setMinimum(1);
    spin_eventServerListenPort->setMaximum((1 << 16) - 1);

    {
        auto label = new QLabel(QSL(
                "Enables the EventServer component which streams "
                "extracted Event data over a TCP socket.\n"
                "The listen address may be a hostname or an IP address. Leave blank to"
                " bind to all local interfaces."));

        label->setWordWrap(true);
        set_widget_font_pointsize_relative(label, -1);

        auto l = new QFormLayout(gb_eventServer);
        l->addRow(label);
        l->addRow(QSL("Listen Address"), le_eventServerListenAddress);
        l->addRow(QSL("Listen Port"), spin_eventServerListenPort);
    }

    widgetLayout->addWidget(gb_jsonRPC);
    widgetLayout->addWidget(gb_eventServer);
    widgetLayout->addWidget(m_bb);

    QObject::connect(m_bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(m_bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void WorkspaceSettingsDialog::populate()
{
    le_expName->setText(m_settings->value(QSL("Experiment/Name")).toString());
    le_expTitle->setText(m_settings->value(QSL("Experiment/Title")).toString());

    cb_ignoreStartupErrors->setChecked(m_settings->value(
            QSL("Experiment/IgnoreVMEStartupErrors")).toBool());

    gb_jsonRPC->setChecked(m_settings->value(QSL("JSON-RPC/Enabled")).toBool());
    le_jsonRPCListenAddress->setText(m_settings->value(QSL("JSON-RPC/ListenAddress")).toString());
    spin_jsonRPCListenPort->setValue(m_settings->value(QSL("JSON-RPC/ListenPort")).toInt());

    gb_eventServer->setChecked(m_settings->value(QSL("EventServer/Enabled")).toBool());
    le_eventServerListenAddress->setText(m_settings->value(QSL("EventServer/ListenAddress")).toString());
    spin_eventServerListenPort->setValue(m_settings->value(QSL("EventServer/ListenPort")).toInt());
}

void WorkspaceSettingsDialog::accept()
{
    m_settings->setValue(QSL("Experiment/Name"), le_expName->text());
    m_settings->setValue(QSL("Experiment/Title"), le_expTitle->text());
    m_settings->setValue(QSL("Experiment/IgnoreVMEStartupErrors"),
                         cb_ignoreStartupErrors->isChecked());

    m_settings->setValue(QSL("JSON-RPC/Enabled"), gb_jsonRPC->isChecked());
    m_settings->setValue(QSL("JSON-RPC/ListenAddress"), le_jsonRPCListenAddress->text());
    m_settings->setValue(QSL("JSON-RPC/ListenPort"), spin_jsonRPCListenPort->value());

    m_settings->setValue(QSL("EventServer/Enabled"), gb_eventServer->isChecked());
    m_settings->setValue(QSL("EventServer/ListenAddress"), le_eventServerListenAddress->text());
    m_settings->setValue(QSL("EventServer/ListenPort"), spin_eventServerListenPort->value());

    m_settings->sync();

    QDialog::accept();
}

void WorkspaceSettingsDialog::reject()
{
    QDialog::reject();
}
