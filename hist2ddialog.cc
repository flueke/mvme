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
    int powMin =  9;
    int powMax = 13;

    for (int i=powMin; i<=powMax; ++i)
    {
        int value = 1 << i;
        ui->comboXResolution->addItem(QString::number(value), value);
        ui->comboYResolution->addItem(QString::number(value), value);
    }

    ui->comboXResolution->setCurrentIndex(1);
    ui->comboYResolution->setCurrentIndex(1);

    auto eventConfigs = m_context->getConfig()->getEventConfigs();
    QStringList eventNames;

    for (auto eventConfig: eventConfigs)
    {
        eventNames << eventConfig->getName();
    }

    ui->eventX->addItems(eventNames);
    ui->eventY->addItems(eventNames);

    onEventXChanged(0);
    onEventYChanged(0);
    onModuleXChanged(0);
    onModuleYChanged(0);
    ui->channelY->setCurrentIndex(1);

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
    int xRes = ui->comboXResolution->currentData().toInt();
    int yRes = ui->comboYResolution->currentData().toInt();

    QString xSource = QString("%1.%2.%3")
        .arg(ui->eventX->currentIndex())
        .arg(ui->moduleX->currentIndex())
        .arg(ui->channelX->currentIndex())
        ;

    QString ySource = QString("%1.%2.%3")
        .arg(ui->eventY->currentIndex())
        .arg(ui->moduleY->currentIndex())
        .arg(ui->channelY->currentIndex())
        ;

    QString name = ui->le_name->text();

    qDebug() << "Hist2D: xRes =" << xRes
        << ", yRes =" << yRes
        << ", xSource =" << xSource
        << ", ySource =" << ySource
        << ", name =" << name
        ;

    auto ret = new Hist2D(xRes, yRes);
    ret->setObjectName(ui->le_name->text());

    ret->setProperty("Hist2D.xAxisSource", xSource);
    ret->setProperty("Hist2D.yAxisSource", ySource);

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
