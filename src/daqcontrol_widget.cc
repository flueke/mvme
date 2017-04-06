#include "daqcontrol_widget.h"
#include "mvme_context.h"
#include "util.h"
#include "ui_daqcontrol_widget.h"

#include <QTimer>

static const int updateInterval = 500;

static void fill_compression_combo(QComboBox *combo)
{
    for (int i=0; i<=9; ++i)
    {
        QString label(QString::number(i));

        switch (i)
        {
            case 0: label = QSL("No compression"); break;
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
{
    ui->setupUi(this);

    connect(ui->pb_start, &QPushButton::clicked, m_context, [this] {
        auto globalMode = m_context->getMode();
        auto daqState = m_context->getDAQState();

        if (globalMode == GlobalMode::DAQ)
        {
            if (daqState == DAQState::Running)
                m_context->pauseDAQ();
            else if (daqState == DAQState::Paused)
                m_context->resumeDAQ();
            else if (daqState == DAQState::Idle)
                m_context->startDAQ();
        }
        else if (globalMode == GlobalMode::ListFile)
        {
            if (daqState == DAQState::Running)
                m_context->pauseReplay();
            else if (daqState == DAQState::Paused)
                m_context->resumeReplay();
            else if (daqState == DAQState::Idle)
                m_context->startReplay();
        }
    });

    connect(ui->pb_oneCycle, &QPushButton::clicked, this, [this] {
        auto globalMode = m_context->getMode();
        if (globalMode == GlobalMode::DAQ)
        {
            m_context->startDAQ(1);
        } else if (globalMode == GlobalMode::ListFile)
        {
            auto daqState = m_context->getDAQState();
            if (daqState == DAQState::Idle)
            {
                m_context->startReplay(1);
            }
            else if (daqState == DAQState::Paused)
            {
                m_context->resumeReplay(1);
            }
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

    connect(ui->cb_writeZip, &QCheckBox::stateChanged, this, [this](int state) {
        auto info = m_context->getListFileOutputInfo();
        bool enabled = (state != Qt::Unchecked);
        info.format = (enabled ? ListFileFormat::ZIP : ListFileFormat::Plain);
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

#if 0
    ui->pb_start->setEnabled(((globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened)
                              || (globalMode == GlobalMode::ListFile && daqState == DAQState::Idle))
                             && (eventProcState == EventProcessorState::Idle)
                            );
#endif

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


#if 0
    ui->pb_oneCycle->setEnabled(daqState == DAQState::Idle
                                && ((globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened)
                                    || (globalMode == GlobalMode::ListFile))
                                && (eventProcState == EventProcessorState::Idle)
                               );
#endif

    ui->gb_listfile->setEnabled(globalMode == GlobalMode::DAQ);

    if (globalMode == GlobalMode::DAQ)
    {
        if (daqState == DAQState::Idle)
            ui->pb_start->setText(QSL("Start"));
        else if (daqState == DAQState::Paused)
            ui->pb_start->setText(QSL("Resume"));
        else
            ui->pb_start->setText(QSL("Pause"));

        ui->pb_oneCycle->setText(QSL("1 Cycle"));
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        if (daqState == DAQState::Idle)
            ui->pb_start->setText(QSL("Start Replay"));
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
        QSignalBlocker b(ui->cb_writeZip);
        ui->cb_writeZip->setChecked(outputInfo.format == ListFileFormat::ZIP);
    }

    {
        QSignalBlocker b(ui->combo_compression);
        ui->combo_compression->setCurrentIndex(outputInfo.compressionLevel);
    }

    // FIXME: use listfile directory here?
    auto filename = stats.listfileFilename;
    filename.remove(m_context->getWorkspaceDirectory() + "/listfiles/");
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
