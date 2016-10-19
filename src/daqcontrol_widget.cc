#include "daqcontrol_widget.h"
#include "mvme_context.h"
#include "util.h"

#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>

DAQControlWidget::DAQControlWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    // DAQ Control buttons
    pb_start    = new QPushButton(QSL("Start"));
    pb_stop     = new QPushButton(QSL("Stop"));
    pb_oneCycle = new QPushButton(QSL("1 Cycle"));

    connect(pb_start, &QPushButton::clicked, m_context, [this] {
        auto daqState = m_context->getDAQState();
        if (daqState == DAQState::Running)
            m_context->pauseDAQ();
        else if (daqState == DAQState::Paused)
            m_context->resumeDAQ();
        else if (daqState == DAQState::Idle)
            m_context->startDAQ();
    });

    connect(pb_oneCycle, &QPushButton::clicked, this, [this] {
        m_context->startDAQ(1);
    });

    connect(pb_stop, &QPushButton::clicked, m_context, &MVMEContext::stopDAQ);

    auto buttonBox = new QHBoxLayout;
    buttonBox->setContentsMargins(2, 2, 2, 2);
    buttonBox->setSpacing(2);
    buttonBox->addWidget(pb_start);
    buttonBox->addWidget(pb_stop);
    buttonBox->addWidget(pb_oneCycle);
    buttonBox->addStretch(1);

    // VME controller and DAQ state
    label_controller = new QLabel;
    label_daqState = new QLabel;
    pb_reconnect = new QPushButton(QSL("Reconnect"));
    auto controllerLayout = new QHBoxLayout;
    controllerLayout->setContentsMargins(0, 0, 0, 0);
    controllerLayout->setSpacing(2);
    controllerLayout->addWidget(label_controller);
    controllerLayout->addWidget(pb_reconnect);
    controllerLayout->addStretch(1);

    connect(pb_reconnect, &QPushButton::clicked, this, [this] {
        auto ctrl = m_context->getController();
        if (ctrl)
            ctrl->close();
        // m_context will call tryOpenController() eventually
    });

    auto labelLayout = new QFormLayout;
    labelLayout->setContentsMargins(2, 2, 2, 2);
    labelLayout->addRow(QSL("VME Controller:"), controllerLayout);
    labelLayout->addRow(QSL("DAQ State:"), label_daqState);

    // Widget layout
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttonBox);
    layout->addLayout(labelLayout);

    connect(m_context, &MVMEContext::daqStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::modeChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::controllerStateChanged, this, &DAQControlWidget::updateWidget);

    updateWidget();
}

void DAQControlWidget::updateWidget()
{
    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();
    auto controllerState = m_context->getController()->getState();

    pb_start->setEnabled(globalMode == GlobalMode::DAQ);
    pb_stop->setEnabled(globalMode == GlobalMode::DAQ && daqState != DAQState::Idle);
    pb_oneCycle->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);

    if (daqState == DAQState::Idle)
        pb_start->setText(QSL("Start"));
    else if (daqState == DAQState::Paused)
        pb_start->setText(QSL("Resume"));
    else
        pb_start->setText(QSL("Pause"));

    label_daqState->setText(DAQStateStrings.value(daqState, QSL("Unknown")));

    label_controller->setText(controllerState == ControllerState::Closed
                              ? QSL("Disconnected")
                              : QSL("Connected"));

    pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);
}
