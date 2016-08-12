#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"

Hist2DDialog::Hist2DDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Hist2DDialog)
{
    ui->setupUi(this);
}

Hist2DDialog::~Hist2DDialog()
{
    delete ui;
}

ChannelSpectro *Hist2DDialog::getHist2D()
{
    auto ret = new ChannelSpectro(ui->spin_xResolution->value(),
                                  ui->spin_yResolution->value());
    ret->setObjectName(ui->le_name->text());
    ret->setProperty("Hist2D.xAxisSource", ui->le_xSource->text());
    ret->setProperty("Hist2D.yAxisSource", ui->le_ySource->text());

    return ret;
}
