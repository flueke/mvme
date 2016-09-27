#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"

#include <QPushButton>
#include <QSignalBlocker>

Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Hist2DDialog)
    , m_context(context)
    , m_histo(histo)
{
    ui->setupUi(this);

    auto validator = new NameValidator(context, histo, this);

    ui->le_name->setValidator(validator);

    if (m_histo)
    {
        ui->le_name->setText(m_histo->objectName());
    }

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
                    ui->le_name->hasAcceptableInput());
    });

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
        ui->le_name->hasAcceptableInput());

    static const int bitsMin =  9;
    static const int bitsMax = 13;

    for (int bits=bitsMin; bits<=bitsMax; ++bits)
    {
        int value = 1 << bits;
        ui->comboXResolution->addItem(QString::number(value), bits);
        ui->comboYResolution->addItem(QString::number(value), bits);
    }

    if (m_histo)
    {
        ui->comboXResolution->setCurrentIndex(m_histo->getXBits() - bitsMin);
        ui->comboYResolution->setCurrentIndex(m_histo->getYBits() - bitsMin);
        ui->comboXResolution->setEnabled(false);
        ui->comboYResolution->setEnabled(false);
    }
    else
    {
        ui->comboXResolution->setCurrentIndex(1);
        ui->comboYResolution->setCurrentIndex(1);
    }

    auto eventConfigs = m_context->getConfig()->getEventConfigs();
    QStringList eventNames;

    for (auto eventConfig: eventConfigs)
    {
        eventNames << eventConfig->getName();
    }

    ui->eventX->addItems(eventNames);
    ui->eventY->addItems(eventNames);

    onEventXChanged(m_histo ? m_histo->getXEventIndex() : 0);
    onEventYChanged(m_histo ? m_histo->getYEventIndex() : 0);
    onModuleXChanged(m_histo ? m_histo->getXModuleIndex() : 0);
    onModuleYChanged(m_histo ? m_histo->getYModuleIndex() : 0);
    ui->channelX->setCurrentIndex(m_histo ? m_histo->getXAddressValue() : 0);
    ui->channelY->setCurrentIndex(m_histo ? m_histo->getYAddressValue() : 1);

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

    if (!m_histo)
    {
        m_histo = new Hist2D(xBits, yBits);
    }
    else
    {
        if (m_histo->property("Hist2D.xAxisSource").toString() != xSource
            || m_histo->property("Hist2D.yAxisSource").toString() != ySource)
        {
            m_histo->clear();
        }
    }

    m_histo->setObjectName(ui->le_name->text());
    m_histo->setProperty("Hist2D.xAxisSource", xSource);
    m_histo->setProperty("Hist2D.yAxisSource", ySource);

    return m_histo;
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

    ui->eventX->setCurrentIndex(index);
    ui->eventY->setCurrentIndex(index);
}
void Hist2DDialog::onModuleXChanged(int index)
{
    ui->moduleX->setCurrentIndex(index);
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
    ui->eventY->setCurrentIndex(index);
}
void Hist2DDialog::onModuleYChanged(int index)
{
    ui->moduleY->setCurrentIndex(index);
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
