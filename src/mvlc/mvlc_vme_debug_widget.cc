#include "mvlc/mvlc_vme_debug_widget.h"
#include "ui_vme_debug_widget.h"

struct WriterUiElements
{
    QLineEdit *le_offset,
              *le_address,
              *le_value;

    QPushButton *pb_loop,
                *pb_write;
};

struct ReaderUiElements
{
    QLineEdit *le_offset,
              *le_address;

    QPushButton *pb_loop,
                *pb_read;


    // optional elements used only for the first reader

    QRadioButton *rb_single,
                 *rb_blt,
                 *rb_mblt;

    QSpinBox *spin_blockReadCount;
};

namespace mesytec
{
namespace mvlc
{

struct VMEDebugWidget::Private
{
    MVLCObject *mvlc;

    QVector<WriterUiElements> ui_writers;
    QVector<ReaderUiElements> ui_readers;
};

VMEDebugWidget::VMEDebugWidget(MVLCObject *mvlc, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
    , ui(std::make_unique<Ui::VMEDebugWidget>())
{
    d->mvlc = mvlc;
    ui->setupUi(this);
    ui->gb_scriptOutput->setVisible(false);

    // Writer1
    {
        WriterUiElements w = {};
        w.le_offset = ui->writeOffset1;
        w.le_address = ui->writeAddress1;
        w.le_value = ui->writeValue1;
        w.pb_loop = ui->writeLoop1;
        w.pb_write = ui->writeWrite1;
        d->ui_writers.push_back(w);
    }

    // Writer2
    {
        WriterUiElements w = {};
        w.le_offset = ui->writeOffset2;
        w.le_address = ui->writeAddress2;
        w.le_value = ui->writeValue2;
        w.pb_loop = ui->writeLoop2;
        w.pb_write = ui->writeWrite2;
        d->ui_writers.push_back(w);
    }

    // Writer3
    {
        WriterUiElements w = {};
        w.le_offset = ui->writeOffset3;
        w.le_address = ui->writeAddress3;
        w.le_value = ui->writeValue3;
        w.pb_loop = ui->writeLoop3;
        w.pb_write = ui->writeWrite3;
        d->ui_writers.push_back(w);
    }

    // Reader1 with block reads
    {
        ReaderUiElements r = {};
        r.le_offset = ui->readOffset1;
        r.le_address = ui->readAddress1;
        r.pb_loop = ui->readLoop1;
        r.pb_read = ui->readRead1;
        r.rb_single = ui->readModeSingle;
        r.rb_blt = ui->readModeBLT;
        r.rb_mblt = ui->readModeMBLT;
        r.spin_blockReadCount = ui->blockReadCount;
        d->ui_readers.push_back(r);
    }

    // Reader2
    {
        ReaderUiElements r = {};
        r.le_offset = ui->readOffset2;
        r.le_address = ui->readAddress2;
        r.pb_loop = ui->readLoop2;
        r.pb_read = ui->readRead2;
        d->ui_readers.push_back(r);
    }

    // Reader3
    {
        ReaderUiElements r = {};
        r.le_offset = ui->readOffset3;
        r.le_address = ui->readAddress3;
        r.pb_loop = ui->readLoop3;
        r.pb_read = ui->readRead3;
        d->ui_readers.push_back(r);
    }
}

VMEDebugWidget::~VMEDebugWidget()
{
}

} // end namespace mvlc
} // end namespace mesytec
