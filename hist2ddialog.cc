#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"

#include <QPushButton>
#include <QSignalBlocker>

Hist2DDialog::Hist2DDialog(MVMEContext *context, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Hist2DDialog)
    , m_context(context)
{
    ui->setupUi(this);

    auto validator = new NameValidator(context, this);

    ui->le_name->setValidator(validator);

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                    ui->le_name->hasAcceptableInput());
    });

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    static const int bitsMin =  9;
    static const int bitsMax = 13;

    for (int bits=bitsMin; bits<=bitsMax; ++bits)
    {
        int value = 1 << bits;
        ui->comboXResolution->addItem(QString::number(value), bits);
        ui->comboYResolution->addItem(QString::number(value), bits);
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
    int xBits = ui->comboXResolution->currentData().toInt();
    int yBits = ui->comboYResolution->currentData().toInt();

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

    qDebug() << "Hist2D: xBits =" << xBits
        << ", yBits =" << yBits
        << ", xSource =" << xSource
        << ", ySource =" << ySource
        << ", name =" << name
        ;

    auto ret = new Hist2D(xBits, yBits);
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
