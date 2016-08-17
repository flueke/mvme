#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"
#include "mvme_context.h"
#include <QSignalBlocker>

Hist2DDialog::Hist2DDialog(MVMEContext *context, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Hist2DDialog)
    , m_context(context)
{
    ui->setupUi(this);

    auto eventConfigs = m_context->getConfig()->getEventConfigs();
    QStringList eventNames;

    for (auto eventConfig: eventConfigs)
    {
        eventNames << eventConfig->getName();
    }

    ui->eventX->addItems(eventNames);
    ui->eventY->addItems(eventNames);

    // event
    connect(ui->eventX, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onEventXChanged);

    connect(ui->eventY, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onEventYChanged);

    // module
    connect(ui->moduleX, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onModuleXChanged);

    connect(ui->moduleY, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onModuleYChanged);

    // channel
    connect(ui->channelX, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onChannelXChanged);

    connect(ui->channelY, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &Hist2DDialog::onChannelYChanged);
}

Hist2DDialog::~Hist2DDialog()
{
    delete ui;
}

Hist2D *Hist2DDialog::getHist2D()
{
    auto ret = new Hist2D(ui->spin_xResolution->value(),
                                  ui->spin_yResolution->value());
    ret->setObjectName(ui->le_name->text());
    //ret->setProperty("Hist2D.xAxisSource", ui->le_xSource->text());
    //ret->setProperty("Hist2D.yAxisSource", ui->le_ySource->text());

    return ret;
}

void Hist2DDialog::onEventXChanged(int index)
{
    ui->moduleX->clear();
    auto eventConfig = m_context->getEventConfigs().at(index);
    if (eventConfig)
    {
        QStringList names;
        for (auto moduleConfig: eventConfig->modules)
        {
            names << moduleConfig->getName();
        }

        ui->moduleX->addItems(names);
    }

    ui->eventY->setCurrentIndex(index);
}
void Hist2DDialog::onModuleXChanged(int index)
{
    ui->channelX->clear();

    auto moduleConfig = m_context->getConfig()->getModuleConfig(ui->eventX->currentIndex(), index);

    if (moduleConfig)
    {
        int nChannels = moduleConfig->getNumberOfChannels();

        for (int c=0; c<nChannels; ++c)
        {
            ui->channelX->addItem(QString::number(c));
        }
    }
}
void Hist2DDialog::onChannelXChanged(int index)
{
}

void Hist2DDialog::onEventYChanged(int index)
{
    ui->moduleY->clear();
    auto eventConfig = m_context->getEventConfigs().at(index);
    if (eventConfig)
    {
        QStringList names;
        for (auto moduleConfig: eventConfig->modules)
        {
            names << moduleConfig->getName();
        }

        ui->moduleY->addItems(names);
    }

    ui->eventX->setCurrentIndex(index);
}
void Hist2DDialog::onModuleYChanged(int index)
{
    ui->channelY->clear();

    auto moduleConfig = m_context->getConfig()->getModuleConfig(ui->eventY->currentIndex(), index);

    if (moduleConfig)
    {
        int nChannels = moduleConfig->getNumberOfChannels();

        for (int c=0; c<nChannels; ++c)
        {
            ui->channelY->addItem(QString::number(c));
        }
    }
}
void Hist2DDialog::onChannelYChanged(int index)
{
}
