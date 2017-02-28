#include "daqcontrol_widget.h"
#include "mvme_context.h"
#include "util.h"
#include "ui_daqcontrol_widget.h"

#include <QTimer>

static const int updateInterval = 500;

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
            m_context->startReplay(1);
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
        bool enabled = (state != Qt::Unchecked);
        m_context->setListFileOutputEnabled(enabled);
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

    ui->pb_start->setEnabled(((globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened)
                              || (globalMode == GlobalMode::ListFile && daqState == DAQState::Idle))
                             && (eventProcState == EventProcessorState::Idle)
                            );

    ui->pb_stop->setEnabled(((globalMode == GlobalMode::DAQ && daqState != DAQState::Idle && controllerState == ControllerState::Opened)
                             || (globalMode == GlobalMode::ListFile && daqState != DAQState::Idle))
                           );

    ui->pb_oneCycle->setEnabled(daqState == DAQState::Idle
                                && ((globalMode == GlobalMode::DAQ && controllerState == ControllerState::Opened)
                                    || (globalMode == GlobalMode::ListFile))
                                && (eventProcState == EventProcessorState::Idle)
                               );

    ui->gb_listfile->setEnabled(globalMode == GlobalMode::DAQ);

    if (globalMode == GlobalMode::DAQ)
    {
        if (daqState == DAQState::Idle)
            ui->pb_start->setText(QSL("Start"));
        else if (daqState == DAQState::Paused)
            ui->pb_start->setText(QSL("Resume"));
        else
            ui->pb_start->setText(QSL("Pause"));
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        ui->pb_start->setText(QSL("Start Replay"));
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

    {
        QSignalBlocker b(ui->cb_writeListfile);
        ui->cb_writeListfile->setChecked(m_context->isListFileOutputEnabled());
    }

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
