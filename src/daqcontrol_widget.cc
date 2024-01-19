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
#include "daqcontrol_widget.h"

#include <QHBoxLayout>
#include <QFileDialog>
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
#include "ui_daq_run_settings_dialog.h"

enum CompressionPreset
{
    NoCompression = 0,      // ZIP, level 0 aka store
    Fast_LZ4 = 1,           // LZ4, level 0 (faster than ZIP level 1)
    Fast_ZIP = 2,           // ZIP, level 1 aka "super fast"
    ZmqServer_Ganil = 3,    // Hack to be able to choose ZMQ pub via the compression combo. TODO: redesign the GUI!
};

static void fill_compression_combo(QComboBox *combo, bool isMVLC)
{
    QSignalBlocker blocker(combo);
    combo->clear();

    combo->addItem(QSL("ZIP (No compression)"), CompressionPreset::NoCompression);

    if (isMVLC)
        combo->addItem(QSL("LZ4 fast compression"), CompressionPreset::Fast_LZ4);

    combo->addItem(QSL("ZIP fast compression"), CompressionPreset::Fast_ZIP);

#ifdef MVLC_HAVE_ZMQ
    if (isMVLC)
        combo->addItem(QSL("ZMQ Publisher"), CompressionPreset::ZmqServer_Ganil);
#endif
}

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

            case CompressionPreset::ZmqServer_Ganil:
                m_listFileOutputInfo.format = ListFileFormat::ZMQ_Ganil;
                // Requested by GANIL: always enable "listfile writing" when ZMQ output is selected.
                cb_writeListfile->setChecked(true);
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
            hbox->addWidget(new QLabel(QSL("Format:")));
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

void DAQControlWidget::setListfileInputFilename(const QString &filename)
{
    m_listfileInputFilename = filename;
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

    QString currentListfilePath;

    switch (globalMode)
    {
        case GlobalMode::DAQ:
            gb_listfile->setTitle(QSL("Listfile Output:"));
            currentListfilePath = stats.listfileFilename;
            break;

        case GlobalMode::ListFile:
            gb_listfile->setTitle(QSL("Listfile Info:"));
            currentListfilePath = m_listfileInputFilename;
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
        else if (outputInfo.format == ListFileFormat::ZMQ_Ganil)
            comboData = CompressionPreset::ZmqServer_Ganil;

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

    //qDebug() << __PRETTY_FUNCTION__ << "listfileFilename=" << filename;

    if (auto settings = make_workspace_settings(m_workspaceDirectory))
    {
        QDir workspaceDir(m_workspaceDirectory);
        auto prefix = workspaceDir.canonicalPath() + '/';

        if (currentListfilePath.startsWith(prefix))
            currentListfilePath.remove(0, prefix.size());
        else
        {
            prefix = workspaceDir.path() + '/';
            if (currentListfilePath.startsWith(prefix))
                currentListfilePath.remove(0, prefix.size());
        }
    }

    if (le_listfileFilename->text() != currentListfilePath)
        le_listfileFilename->setText(currentListfilePath);


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
    , ui(new Ui::DaqRunSettingsDialog)
    , m_settings(settings)
{
    ui->setupUi(this);
    setWindowTitle(QSL("DAQ Run Settings"));
    auto re_prefixSuffix = QRegularExpression(QSL("^[^\\\\/]+$"));
    ui->le_prefix->setValidator(new QRegularExpressionValidator(re_prefixSuffix, ui->le_prefix));
    ui->le_suffix->setValidator(new QRegularExpressionValidator(re_prefixSuffix, ui->le_suffix));

    ui->le_prefix->setText(settings.prefix);
    ui->le_suffix->setText(settings.suffix);
    ui->cb_useRunNumber->setChecked(settings.flags & ListFileOutputInfo::UseRunNumber);
    ui->cb_useTimestamp->setChecked(settings.flags & ListFileOutputInfo::UseTimestamp);
    ui->le_formatString->setText(settings.fmtStr);
    ui->spin_runNumber->setValue(settings.runNumber);
    ui->rb_dontSplit->setChecked(true);
    ui->rb_splitBySize->setChecked(settings.flags & ListFileOutputInfo::SplitBySize);
    ui->rb_splitByTime->setChecked(settings.flags & ListFileOutputInfo::SplitByTime);
    ui->spin_splitSize->setValue(settings.splitSize / Megabytes(1));
    ui->spin_splitTime->setValue(settings.splitTime.count());

    connect(ui->gb_prefixSuffix, &QGroupBox::toggled,
            [this](bool on)
            {
                ui->gb_formatString->setChecked(!on);
                updateExample();
            });

    connect(ui->gb_formatString, &QGroupBox::toggled,
            [this](bool on)
            {
                ui->gb_prefixSuffix->setChecked(!on);
                updateExample();
            });

    ui->gb_prefixSuffix->setChecked(!(settings.flags & ListFileOutputInfo::UseFormatStr));

    connect(ui->le_prefix, &QLineEdit::textChanged, this, [this] { updateExample(); });
    connect(ui->le_suffix, &QLineEdit::textChanged, this, [this] { updateExample(); });
    connect(ui->le_formatString, &QLineEdit::textChanged, this, [this] { updateExample(); });
    connect(ui->spin_runNumber, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateExample(); });
    connect(ui->cb_useRunNumber, &QCheckBox::toggled, this, [this] { updateExample(); });
    connect(ui->cb_useTimestamp, &QCheckBox::toggled, this, [this] { updateExample(); });
    connect(ui->spin_splitSize, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateExample(); });
    connect(ui->spin_splitTime, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateExample(); });
    connect(ui->rb_dontSplit, &QRadioButton::toggled, this, [this] { updateExample(); });
    connect(ui->rb_splitBySize, &QRadioButton::toggled, this, [this] { updateExample(); });
    connect(ui->rb_splitByTime, &QRadioButton::toggled, this, [this] { updateExample(); });

    // zmq ganil publisher options
    ui->spin_zmqGanilBindPort->setValue(settings.options.value("zmq_ganil_bind_port", "5575").toUInt());

    connect(ui->bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateExample();
}

DAQRunSettingsDialog::~DAQRunSettingsDialog()
{
    delete ui;
}

void DAQRunSettingsDialog::updateSettings()
{
    auto &s = m_settings;
    s.prefix = ui->le_prefix->text();
    s.suffix = ui->le_suffix->text();
    s.fmtStr = ui->le_formatString->text();
    s.runNumber = ui->spin_runNumber->value();
    s.splitSize = ui->spin_splitSize->value() * Megabytes(1);
    s.splitTime = std::chrono::seconds(ui->spin_splitTime->value());

    s.flags &= ~(ListFileOutputInfo::UseRunNumber | ListFileOutputInfo::UseTimestamp);

    if (ui->cb_useRunNumber->isChecked())
        s.flags |= ListFileOutputInfo::UseRunNumber;

    if (ui->cb_useTimestamp->isChecked())
        s.flags |= ListFileOutputInfo::UseTimestamp;

    if (ui->gb_formatString->isChecked())
        s.flags |= ListFileOutputInfo::UseFormatStr;
    else
        s.flags &= ~ListFileOutputInfo::UseFormatStr;

    s.flags &= ~(ListFileOutputInfo::SplitBySize | ListFileOutputInfo::SplitByTime);

    if (ui->rb_splitByTime->isChecked())
        s.flags |= ListFileOutputInfo::SplitByTime;

    if (ui->rb_splitBySize->isChecked())
        s.flags |= ListFileOutputInfo::SplitBySize;

    s.options.insert("zmq_ganil_bind_port", ui->spin_zmqGanilBindPort->value());
}

void DAQRunSettingsDialog::updateExample()
{
    try
    {
        ui->le_formatError->clear();
        updateSettings();
        auto filename = generate_output_filename(m_settings);
        auto basename = QFileInfo(filename).completeBaseName();
        auto extension = QFileInfo(filename).completeSuffix();

        if (m_settings.flags & (ListFileOutputInfo::SplitByTime | ListFileOutputInfo::SplitBySize))
            filename = basename + "_part007." + extension;

        ui->le_exampleName->setText(filename);
        ui->bb->button(QDialogButtonBox::StandardButton::Ok)->setEnabled(true);
    }
    catch (const std::runtime_error &e)
    {
        ui->le_formatError->setText(e.what());
        ui->bb->button(QDialogButtonBox::StandardButton::Ok)->setEnabled(false);
    }
}

void DAQRunSettingsDialog::accept()
{
    updateSettings();
    QDialog::accept();
}

QLabel *make_explanation_label(const QString &str)
{
    auto result = new QLabel(str);
    result->setWordWrap(true);
    set_widget_font_pointsize_relative(result, -1);
    return result;
}

WorkspaceSettingsDialog::WorkspaceSettingsDialog(const std::shared_ptr<QSettings> &settings,
                                                 QWidget *parent)
    : QDialog(parent)
    , gb_jsonRPC(new QGroupBox(QSL("Enable JSON-RPC Server")))
    , gb_eventServer(new QGroupBox(QSL("Enable Event Server")))
    , le_jsonRPCListenAddress(new QLineEdit)
    , le_eventServerListenAddress(new QLineEdit)
    , le_listfileDir(new QLineEdit)
    , pb_listfileDir(new QPushButton(QSL("Select")))
    , spin_jsonRPCListenPort(new QSpinBox)
    , spin_eventServerListenPort(new QSpinBox)
    , cb_ignoreStartupErrors(new QCheckBox("Ignore VME Init Startup Errors"))
    , m_bb(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_settings(settings)
{
    setWindowTitle("mvme workspace settings");

    auto widgetLayout = new QVBoxLayout(this);

    // General settings: ignore startup errors, listfile output directory
    {
        auto gb = new QGroupBox(QSL("General"));
        auto l = new QFormLayout(gb);

        auto label = make_explanation_label(QSL(
                "If enabled VME read/write errors during the DAQ"
                " start sequence will not abort the DAQ run."));

        l->addRow(label);
        l->addRow(cb_ignoreStartupErrors);

        auto l_listfileOutput = new QHBoxLayout;
        l_listfileOutput->addWidget(le_listfileDir);
        l_listfileOutput->addWidget(pb_listfileDir);

        l->addRow(QSL("Listfile output directory"), l_listfileOutput);

        widgetLayout->addWidget(gb);
    }

    // Groupbox ExperimentName and ExperimentTitle
    {
        auto gb = new QGroupBox(QSL("Experiment Info"));
        le_expName = new QLineEdit(this);
        le_expTitle = new QLineEdit(this);
        auto l = new QFormLayout(gb);

        auto label = make_explanation_label(QSL(
            "Information transmitted in the EventServer protocol. Used by mvme_root_client"
            "to generate class and file names."));

        l->addRow(label);
        l->addRow(QSL("Experiment Name"), le_expName);
        l->addRow(QSL("Experiment Title"), le_expTitle);

        widgetLayout->addWidget(gb);
    }

    // JSONRPC
    gb_jsonRPC->setCheckable(true);
    spin_jsonRPCListenPort->setMinimum(1);
    spin_jsonRPCListenPort->setMaximum((1 << 16) - 1);

    {
        auto label = make_explanation_label(QSL(
                "Enables a built-in JSON-RPC server allowing remote control"
                " and remote status queries.\n"
                "The listen address may be a hostname or an IP address. Leave blank to"
                " bind to all local interfaces."));

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
        auto label = make_explanation_label(QSL(
                "Enables the EventServer component which streams "
                "extracted Event data over a TCP socket.\n"
                "The listen address may be a hostname or an IP address. Leave blank to"
                " bind to all local interfaces."));

        auto l = new QFormLayout(gb_eventServer);
        l->addRow(label);
        l->addRow(QSL("Listen Address"), le_eventServerListenAddress);
        l->addRow(QSL("Listen Port"), spin_eventServerListenPort);
    }

    widgetLayout->addWidget(gb_jsonRPC);
    widgetLayout->addWidget(gb_eventServer);
    widgetLayout->addStretch(1);
    widgetLayout->addWidget(m_bb);

    connect(pb_listfileDir, &QPushButton::clicked,
            this, &WorkspaceSettingsDialog::selectListfileDir);

    connect(m_bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populate();
}

void WorkspaceSettingsDialog::selectListfileDir()
{
    auto dirpath = QFileDialog::getExistingDirectory(
        this, QSL("Select listfile output directory"), le_listfileDir->text());

    if (!dirpath.isEmpty())
        le_listfileDir->setText(dirpath);
}

void WorkspaceSettingsDialog::populate()
{
    le_listfileDir->setText(m_settings->value("ListFileDirectory").toString());

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
    m_settings->setValue("ListFileDirectory", le_listfileDir->text());

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
