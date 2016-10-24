#include "daqcontrol_widget.h"
#include "mvme_context.h"
#include "util.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>

DAQControlWidget::DAQControlWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    // DAQ Control buttons
    pb_start    = new QPushButton(QSL("Start"));
    pb_stop     = new QPushButton(QSL("Stop"));
    pb_oneCycle = new QPushButton(QSL("1 Cycle"));

    connect(pb_start, &QPushButton::clicked, m_context, [this] {
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

    // Listfile
    gbListFile = new QGroupBox(QSL("Listfile Output"));
    auto listFileLayout = new QFormLayout(gbListFile);

    label_listFileDir = new QLabel;
    label_listFileDir->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    cb_writeListFile = new QCheckBox("Write listfile");

    connect(cb_writeListFile, &QCheckBox::stateChanged, this, [this](int state) {
        bool enabled = (state != Qt::Unchecked);
        m_context->getConfig()->setListFileOutputEnabled(enabled);
    });

    auto pb_outputDirectory = new QPushButton("Select directory");

    connect(pb_outputDirectory, &QPushButton::clicked, this, [this] {
        auto dirName = QFileDialog::getExistingDirectory(this, "Select output directory",
                                                         m_context->getConfig()->getListFileOutputDirectory());
        if (!dirName.isEmpty())
        {
            QFontMetrics fm(label_listFileDir->font());
            auto labelText = fm.elidedText(dirName, Qt::ElideMiddle, label_listFileDir->width());

            label_listFileDir->setText(labelText);
            label_listFileDir->setToolTip(dirName);
            m_context->getConfig()->setListFileOutputDirectory(dirName);
        }
    });

    auto hbox = new QHBoxLayout;
    hbox->addWidget(cb_writeListFile);
    hbox->addWidget(pb_outputDirectory);
    hbox->addStretch(1);

    listFileLayout->addRow(hbox);
    listFileLayout->addRow(label_listFileDir);

    // Widget layout
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addLayout(buttonBox);
    layout->addLayout(labelLayout);
    layout->addWidget(gbListFile);

    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    connect(m_context, &MVMEContext::daqStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::modeChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::controllerStateChanged, this, &DAQControlWidget::updateWidget);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &DAQControlWidget::updateWidget);

    updateWidget();
}

void DAQControlWidget::updateWidget()
{
    auto globalMode = m_context->getMode();
    auto daqState = m_context->getDAQState();
    auto controllerState = m_context->getController()->getState();

    pb_start->setEnabled((globalMode == GlobalMode::DAQ
                          && controllerState == ControllerState::Opened)
                         || (globalMode == GlobalMode::ListFile
                             && daqState == DAQState::Idle)
                         );

    pb_stop->setEnabled((globalMode == GlobalMode::DAQ
                         && daqState != DAQState::Idle
                         && controllerState == ControllerState::Opened)
                        || (globalMode == GlobalMode::ListFile
                            && daqState != DAQState::Idle)
                       );

    pb_oneCycle->setEnabled(globalMode == GlobalMode::DAQ
                            && daqState == DAQState::Idle
                            && controllerState == ControllerState::Opened);

    gbListFile->setEnabled(globalMode == GlobalMode::DAQ);

    if (globalMode == GlobalMode::DAQ)
    {
        if (daqState == DAQState::Idle)
            pb_start->setText(QSL("Start"));
        else if (daqState == DAQState::Paused)
            pb_start->setText(QSL("Resume"));
        else
            pb_start->setText(QSL("Pause"));
    }
    else if (globalMode == GlobalMode::ListFile)
    {
        pb_start->setText(QSL("Start Replay"));
    }

    label_daqState->setText(DAQStateStrings.value(daqState, QSL("Unknown")));

    label_controller->setText(controllerState == ControllerState::Closed
                              ? QSL("Disconnected")
                              : QSL("Connected"));

    pb_reconnect->setEnabled(globalMode == GlobalMode::DAQ && daqState == DAQState::Idle);

    auto config = m_context->getConfig();

    {
        QSignalBlocker b(cb_writeListFile);
        cb_writeListFile->setChecked(config->isListFileOutputEnabled());
    }

    QFontMetrics fm(label_listFileDir->font());
    auto dirName = config->getListFileOutputDirectory();
    auto labelText = fm.elidedText(dirName, Qt::ElideMiddle, label_listFileDir->width());
    label_listFileDir->setText(labelText);
    label_listFileDir->setToolTip(dirName);
}
