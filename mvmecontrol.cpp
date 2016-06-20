#include "mvmecontrol.h"
#include "ui_mvmecontrol.h"
#include "mvme.h"
#include "datathread.h"
#include "diagnostics.h"
#include "realtimedata.h"

#include "QLabel"
#include "QString"
#include "QCheckBox"
#include "QLineEdit"
#include "QSpinBox"
#include "QPushButton"
#include "QTimer"
#include "QTextEdit"
#include "QTextStream"
#include "QFileDialog"
#include "QFile"
#include "QMessageBox"
#include "QComboBox"



mvmeControl::mvmeControl(mvme *theApp, QWidget *parent) :
    QWidget(parent),
    theApp(theApp),
    ui(new Ui::mvmeControl)
{
    theApp = (mvme*)parent;
    ui->setupUi(this);
    counter = 0;
    dontUpdate = false;

    ui->readMblt->setEnabled(false); // TODO: add mblt support to vmusb, then re-enable
    ui->bufTerminators->setEnabled(false);

    qDebug("sizeof(long)=%d", sizeof(long));
}

mvmeControl::~mvmeControl()
{
    delete ui;
}

void mvmeControl::setValues()
{
    if(dontUpdate)
        return;
}


void mvmeControl::getValues()
{
#ifdef VME_CONTROLLER_WIENER
   theApp->vu->readAllRegisters();
   refreshDisplay();
#endif

}

void mvmeControl::changeMode()
{
#ifdef VME_CONTROLLER_WIENER
    if(dontUpdate)
        return;

    qDebug("changeMode()");

    int mode = 0;

    // buffer length
    mode &= 0x0000FFF0;
    mode |= ui->bufSize->currentIndex();

    // mixed buffers
    mode &= 0x0000FFDF;
    if(ui->mixedBuffers->isChecked())
        mode |= 0x20;

    // FIXME: According to the documentation bit 6 should be 'FreeSclrDmp', not something related to buffer terminators.
#if 0 
    // separator option
    mode &= 0x0000FFBF;
    if(ui->bufTerminators->currentIndex() == 1)
        mode |= 0x40;
#endif

    // 32 bit aligned buffers
    mode &= 0x0000FF7F;
    if(ui->align32->isChecked())
        mode |= 0x80;

    // header option
    mode &= 0x0000F7FF;
    if(ui->headerOption->isChecked())
        mode |= 0x100;

    // bus request option
    mode &= 0x00008FFF;
    mode |= 0x1000 * ui->busRequest->value();

    int ret = theApp->vu->setMode(mode);

    refreshDisplay();

    qDebug("changed Mode to: %x, should be: %x", ret, mode);

#endif
}

void mvmeControl::changeNumber()
{
#if 0
    if(dontUpdate)
        return;

    qDebug("changeNumber()");

    int val = (int)numExtractMask->value();
    int ret = theApp->vu->setNumberMask(val);
    refreshDisplay();

    qDebug("changed Extract Number Mask to: %x, should be: %x", ret, val);
#endif
}

void mvmeControl::changeBulkTransfer()
{
    if(dontUpdate)
        return;
    qDebug("changeBulk()");
}

void mvmeControl::changeLed(int led)
{
#if 0
     if(dontUpdate)
        return;

    int val = theApp->vu->getLedSources();
    if(led < 8){
        val &= 0xFFFFFF00;
        val |= led;
        if(tyInv->isChecked())
            val |= 0x08;
        if(tyLatch->isChecked())
            val |= 0x10;
    }
    if(led > 7 && led < 16){
        val &= 0xFFFF00FF;
        val |= ((led-8) * 0x100);
        if(rInv->isChecked())
            val |= 0x0800;
        if(rLatch->isChecked())
            val |= 0x1000;
    }
    if(led > 15 && led < 24){
        val &= 0xFF00FFFF;
        val |= ((led-16) * 0x10000);
        if(gInv->isChecked())
            val |= 0x080000;
        if(gLatch->isChecked())
            val |= 0x100000;
    }
    if(led > 23 && led < 32){
        val &= 0x00FFFFFF;
        val |= ((led-24) * 0x1000000);
        if(byInv->isChecked())
            val |= 0x08000000;
        if(byLatch->isChecked())
            val |= 0x10000000;
    }
    if(led == 50 || led == 51){
        val &= 0xFFFFFF00;
        if(tyInv->isChecked())
            val |= 0x08;
        if(tyLatch->isChecked())
            val |= 0x10;
    }
    if(led == 52 || led == 53){
        val &= 0xFFFF00FF;
        if(rInv->isChecked())
            val |= 0x0800;
        if(rLatch->isChecked())
            val |= 0x1000;
    }
    if(led == 54 || led == 55){
        val &= 0xFF00FFFF;
        if(gInv->isChecked())
            val |= 0x080000;
        if(gLatch->isChecked())
            val |= 0x100000;
    }
    if(led == 56 || led == 57){
        val &= 0x00FFFFFF;
        if(byInv->isChecked())
            val |= 0x08000000;
        if(byLatch->isChecked())
            val |= 0x10000000;
    }
    qDebug("changeLed %d (%x)", led, val);
    theApp->vu->setLedSources(val);
#endif
}

void mvmeControl::changeSource()
{
    if(dontUpdate)
        return;
    qDebug("changeSource()");
}

/*!
    \fn mvmeControl::refreshDisplay(void)
 */
void mvmeControl::refreshDisplay(void)
{
#ifdef VME_CONTROLLER_WIENER
    int val, val2;
    QString str;

    dontUpdate = true;

#if 0
    // raw values
    str.sprintf("%x",theApp->vu->getFirmwareId());
    fwId->setText(str);
    str.sprintf("%x",theApp->vu->getMode());
    globMod->setText(str);
    str.sprintf("%x",theApp->vu->getDaqSettings());
    daqSet->setText(str);
    str.sprintf("%x",theApp->vu->getLedSources());
    ledSource->setText(str);
    str.sprintf("%x",theApp->vu->getDeviceSources());
    devSource->setText(str);
    str.sprintf("%x",theApp->vu->getDggA());
    dggA->setText(str);
    str.sprintf("%x",theApp->vu->getDggB());
    dggB->setText(str);
    str.sprintf("%x",theApp->vu->getScalerAdata());
    scalerAdata->setText(str);
    str.sprintf("%x",theApp->vu->getScalerBdata());
    scalerBdata->setText(str);
    str.sprintf("%x",theApp->vu->getNumberMask());
    numMask->setText(str);
    str.sprintf("%x",theApp->vu->getIrq(0));
    irq12->setText(str);
    str.sprintf("%x",theApp->vu->getIrq(1));
    irq34->setText(str);
    str.sprintf("%x",theApp->vu->getIrq(2));
    irq56->setText(str);
    str.sprintf("%x",theApp->vu->getIrq(3));
    irq78->setText(str);
    str.sprintf("%x",theApp->vu->getDggSettings());
    extDgg->setText(str);
    str.sprintf("%x",theApp->vu->getUsbSettings());
    usbBulk->setText(str);

    // firmware ID
    val = theApp->vu->getFirmwareId();
    str.sprintf("%x",val);
    idRaw->setText(str);

    str.sprintf("%x", val & 0xFF);
    idMin->setText(str);

    val2 = val & 0xFF00;
    val2 /= 0x100;
    str.sprintf("%x", val2);
    idMaj->setText(str);

    val2 = val & 0xFF0000;
    val2 /= 0x10000;
    str.sprintf("%x", val2);
    idBeta->setText(str);

    val2 = val & 0xF000000;
    val2 /= 0x1000000;
    str.sprintf("%x", val2);
    idYear->setText(str);

    val2 = val & 0xF0000000;
    val2 /= 0x10000000;
    str.sprintf("%x", val2);
    idMonth->setText(str);
#endif

    // Global Mode
    val = theApp->vu->getMode();

    qDebug("refreshDisplay: mode=%x", val);

    // Buffer Options
    val2 = val & 0x0F;
	ui->bufSize->setCurrentIndex(val2);
    val2 = val & 0x20;
    if(val2)
        ui->mixedBuffers->setChecked(true);
    else
        ui->mixedBuffers->setChecked(false);

    val2 = val & 0x40;
	if(val2)
		ui->bufTerminators->setCurrentIndex(1);
	else
		ui->bufTerminators->setCurrentIndex(0);

    val2 = val & 0x80;
    if(val2)
        ui->align32->setChecked(true);
    else
        ui->align32->setChecked(false);

    val2 = val & 0x100;
    if(val2)
        ui->headerOption->setChecked(true);
    else
        ui->headerOption->setChecked(false);

    val2 = val & 0x7000;
    val2 /= 0x1000;
	ui->busRequest->setValue(val2);
    dontUpdate = false;
#endif
}

void mvmeControl::readVme()
{
    bool ok;
    QString str, str2;
    long ret = 0;
    quint32 data[256];
    str = ui->readOffset->text();
    long offset = str.toInt(&ok, 0);
    str = ui->readAddr->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);

    if(ui->readBlt->isChecked() || ui->readMblt->isChecked()){
        unsigned char size;
        str = ui->readBltsize->text();
        size = str.toUInt(&ok, 0);
        if(ui->readBlt->isChecked()){
            ret = theApp->vu->vmeBltRead32(addr, size*4, data);
            qDebug("read %ld bytes from bus by BLT", ret);
        }
        else{
            //ret = theApp->vu->vmeMbltRead32(addr, size*4, data);
            //qDebug("read %ld bytes from bus by MBLT", ret);
        }
        ui->bltResult->clear();
        str.sprintf("");
        for(unsigned char c=0;c<ret/4;c++){
            qDebug("disp: %08lx", data[c]);
            str2.sprintf("%d: %08lx\n", c, data[c]);
            str.append(str2);
        }
        ui->bltResult->setText(str);
    }
    else{
        if(ui->read16->isChecked())
            ret = theApp->vu->vmeRead16(addr, (long*)data);
        else
            ret = theApp->vu->vmeRead32(addr, (long*)data);

        str.sprintf("%lx", data[0]);
        ui->readValue->setText(str);
        qDebug("read %lx from addr %lx, ret val = %ld", data[0], addr, ret);
        if(ui->readLoop->isChecked())
            QTimer::singleShot( 100, this, SLOT(readVme()) );
    }
}

void mvmeControl::readVme2()
{
    bool ok;
    QString str, str2;
    long ret;
    long data[256];
    str = ui->readOffset_2->text();
    long offset = str.toInt(&ok, 0);
    str = ui->readAddr_2->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);

    if(ui->read16_2->isChecked())
        ret = theApp->vu->vmeRead16(addr, data);
    else
        ret = theApp->vu->vmeRead32(addr, data);

    str.sprintf("%lx", data[0]);
    ui->readValue_2->setText(str);
    qDebug("read %lx from addr %lx, ret val = %ld", data[0], addr, ret);
    if(ui->readLoop_2->isChecked())
        QTimer::singleShot( 100, this, SLOT(readVme2()) );
}

void mvmeControl::readVme3()
{
    bool ok;
    QString str, str2;
    long ret;
    long data[256];
    str = ui->readOffset_3->text();
    long offset = str.toInt(&ok, 0);
    str = ui->readAddr_3->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);

    if(ui->read16_3->isChecked())
            ret = theApp->vu->vmeRead16(addr, data);
        else
            ret = theApp->vu->vmeRead32(addr, data);

    str.sprintf("%lx", data[0]);
    ui->readValue_3->setText(str);
     qDebug("read %lx from addr %lx, ret val = %ld", data[0], addr, ret);
    if(ui->readLoop_3->isChecked())
        QTimer::singleShot( 100, this, SLOT(readVme3()) );
}

void mvmeControl::writeVme()
{
    bool ok;
    QString str;
    long ret;
    str = ui->writeOffset->text();
    long offset = str.toInt(&ok, 0);
    str = ui->writeAddr->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);
    str =ui-> writeValue->text();
    long val = str.toUInt(&ok, 0);
    if(ui->write16->isChecked())
        ret = theApp->vu->vmeWrite16(addr, val);
    else
        ret = theApp->vu->vmeWrite32(addr, val);
    qDebug("wrote %lx to addr %lx, ret val = %ld", val, addr, ret);
    if(ui->writeLoop->isChecked())
        QTimer::singleShot( 100, this, SLOT(writeVme()) );
}

void mvmeControl::writeVme2()
{
    bool ok;
    QString str;
    long ret;
    str = ui->writeOffset_2->text();
    long offset = str.toInt(&ok, 0);
    str = ui->writeAddr_2->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);
    str =ui-> writeValue_2->text();
    long val = str.toUInt(&ok, 0);
    if(ui->write16_2->isChecked())
        ret = theApp->vu->vmeWrite16(addr, val);
    else
        ret = theApp->vu->vmeWrite32(addr, val);
    qDebug("wrote %lx to addr %lx, ret val = %ld", val, addr, ret);
    if(ui->writeLoop_2->isChecked())
        QTimer::singleShot( 100, this, SLOT(writeVme2()) );
}

void mvmeControl::writeVme3()
{
    bool ok;
    QString str;
    long ret;
    str = ui->writeOffset_3->text();
    long offset = str.toInt(&ok, 0);
    str = ui->writeAddr_3->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);
    str =ui-> writeValue_3->text();
    long val = str.toUInt(&ok, 0);
    if(ui->write16_3->isChecked())
        ret = theApp->vu->vmeWrite16(addr, val);
    else
        ret = theApp->vu->vmeWrite32(addr, val);
    qDebug("wrote %lx to addr %lx, ret val = %ld", val, addr, ret);
    if(ui->writeLoop_3->isChecked())
        QTimer::singleShot( 100, this, SLOT(writeVme3()) );
}

void mvmeControl::readLoopSlot()
{
    if(ui->readLoop->isChecked()){
        QTimer::singleShot( 100, this, SLOT(readVme()) );
    }
}

void mvmeControl::readLoopSlot2()
{
    if(ui->readLoop_2->isChecked()){
        QTimer::singleShot( 100, this, SLOT(readVme2()) );
    }
}

void mvmeControl::readLoopSlot3()
{
    if(ui->readLoop_3->isChecked()){
        QTimer::singleShot( 100, this, SLOT(readVme3()) );
    }
}


void mvmeControl::writeLoopSlot()
{
    if(ui->writeLoop->isChecked()){
        QTimer::singleShot( 100, this, SLOT(writeVme()) );
    }
}

void mvmeControl::writeLoopSlot2()
{
    if(ui->writeLoop_2->isChecked()){
        QTimer::singleShot( 100, this, SLOT(writeVme2()) );
    }
}

void mvmeControl::writeLoopSlot3()
{
    if(ui->writeLoop_2->isChecked()){
        QTimer::singleShot( 100, this, SLOT(writeVme3()) );
    }
}

void mvmeControl::saveStack()
{
   QString s = QFileDialog::getSaveFileName(this,
                                            "Choose a file",
                                            "/home",
                                            "Stack files (*.stk)"
    );
    QFile f(s);
    if(!f.open(QIODevice::ReadWrite))
        return;

    QTextStream t(&f);
    t << ui->stackDisplay->toPlainText();
    f.flush();
    f.close();
}

void mvmeControl::activateStack()
{
    bool ok;
    QString s, s2;
    const uint stackMaxSize = 0x1000;
    long stack[stackMaxSize];
    s=ui->stackDisplay->toPlainText();
    QTextStream t(&s, QIODevice::ReadWrite);

    if(ui->stackNumber->currentText() == "Memory")
    {
        uint i=0;

        while (!t.atEnd() && i < stackMaxSize) {
            long value = 0;
            t >> value;

            if (t.status() != QTextStream::Ok)
            {
                break;
            }

            stack[i++] = value;
        }

        uint stackSize = i-1;

        for(i=0;i<stackSize;i++){
            theApp->vu->vmeWrite16(stack[i], stack[i+1]);
            i++;
            qDebug("wrote %lx to %lx", stack[i], stack[i-1]);
        }
    } else
    {
        uint i = 1; // leave room to store the size of the stack

        while (!t.atEnd() && i < stackMaxSize) {
            long value = 0;
            t >> value;

            if (t.status() != QTextStream::Ok)
            {
                break;
            }

            stack[i++] = value;
        }

        uint stackSize = i-1;

        stack[0] = stackSize;

        for(i=1;i<stackSize;i++){
            qDebug("%lx", stack[i]);
        }

        if(ui->stackNumber->currentText() == "Execute")
        {
            int ret = theApp->vu->stackExecute(stack);
            qDebug("stackExecute returned %x", ret);

            /*
            s2.sprintf("%08x", ret);
            s2.append("\n");
            */

            for(int i = 0; i < ret/4  ; i++){
                qDebug("%08lx", stack[i]);
                s.sprintf("%08lx", stack[i]);
                s2.append(s);
                s2.append("\n");
            }
            ui->resultDisplay->setText(s2);
        }
        else
        {
            bool ok;
            int val = ui->stackNumber->currentText().toInt(&ok, 0);
            int ret = theApp->vu->stackWrite(val, stack);
            qDebug("stackWrite: returned %d", ret);
        }
    }
}

void mvmeControl::loadStack()
{
   QString s = QFileDialog::getOpenFileName(this,
                                            "Choose a file",
                                            "/home",
                                            "Stack files (*.stk)"
     );
    QFile f(s);
    if(!f.open(QIODevice::ReadWrite))
        return;

    QTextStream t(&f);
    QString s2;
    s.sprintf("");
    if(ui->stackNumber->currentText() == "Memory"){
        while(!t.atEnd()){
            t >> s2;
            s.append(s2);
            s.append(" ");
            t >> s2;
            s.append(s2);
            s.append("\n");
        }
    }
    else{
        while(!t.atEnd()){
        t >> s2;
        s.append(s2);
        s.append("\n");
        }
    }
    ui->stackDisplay->setText(s);
    f.close();
}

void mvmeControl::loadData()
{
#ifdef VME_CONTROLLER_WIENER
    QString s = QFileDialog::getOpenFileName(
            this,
            "Choose a file",
            QString(),
            "Data files (*.txt)");





    QFile f(s);
    if(!f.open(QIODevice::ReadOnly))
        return;

    QTextStream t(&f);
    QString s2;
    s.sprintf("");
    if(ui->stackNumber->currentText() == "Memory"){
        while(!t.atEnd()){
            t >> s2;
            s.append(s2);
            s.append(" ");
            t >> s2;
            s.append(s2);
            s.append("\n");
        }
    }
    else{
        while(!t.atEnd()){
        t >> s2;
        s.append(s2);
        s.append("\n");
        }
    }
    ui->stackDisplay->setText(s);
    f.close();
#endif
}

void mvmeControl::sendData()
{
/*    QString data = ui->dataDisplay->text();
    parseDataText(data);
*/}

/*!
    \fn mvmeControl::parseDataText()
 */
void mvmeControl::parseDataText(QString text)
{

}

void mvmeControl::readStack()
{
#ifdef VME_CONTROLLER_WIENER
    bool ok;
    long stack[0x1000];
    QString s, s2;
    int val = ui->stackNumber->currentText().toInt(&ok, 0);
    int ret = theApp->vu->stackRead(val, &stack[0]);
    qDebug("read %d bytes from stack %d", ret, val);

    for(int i = 0; i < ret/2; i++){
        s2.sprintf("0x%lx",stack[i]);
        s.append(s2);
        s.append("\n");
    }
    ui->resultDisplay->setPlainText(s);
#endif
}

void mvmeControl::setIrq()
{
#ifdef VME_CONTROLLER_WIENER
    bool ok;
    int stack, irq, id, vector;

    stack = ui->stack1->value();
    irq = ui->irq1->value();
    id = ui->irqId1->text().toInt(&ok, 0);
    vector = id + 256*irq + 4096*stack;
    qDebug("IRQ Vector: %x", vector);
    theApp->vu->setIrq(0, vector);

    stack = ui->stack2->value();
    irq = ui->irq2->value();
    id = ui->irqId2->text().toInt(&ok, 0);
    vector = id + 256*irq + 4096*stack;
    qDebug("IRQ Vector: %x", vector);
    theApp->vu->setIrq(1, vector);

    stack = ui->stack3->value();
    irq = ui->irq3->value();
    id = ui->irqId3->text().toInt(&ok, 0);
    vector = id + 256*irq + 4096*stack;
    qDebug("IRQ Vector: %x", vector);
    theApp->vu->setIrq(2, vector);
#endif
}

void mvmeControl::readBuffer()
{
    QString s, s2;
    int data[1000];
    int ret;
    ret = theApp->vu->readLongBuffer(data);
    for(int i = 0; i < ret; i++){

        s2.sprintf("%08x",data[i]);
        s.append(s2);
        s.append("\n");
    }
    ui->dataDisplay->setText(s);
    s.sprintf("%d", ret);
    ui->readBytes->setText(s);
}

void mvmeControl::startStop(bool val)
{
    int ret;
    if(val){
        ret = theApp->vu->usbRegisterWrite(1, 0);
        qDebug("Stop: 0");
    }
    else{
        ret = theApp->vu->usbRegisterWrite(1, 1);
        qDebug("Start: 1");
    }
    qDebug("wrote %d bytes to register", ret);
}

void mvmeControl::setScalerValue()
{
#ifdef VME_CONTROLLER_WIENER
#if 0
    bool ok;
    QString str;
    long ret;

    str = ui->scalerPeriod->text();
    unsigned int period = str.toInt(&ok, 0);
    ret = theApp->vu->setScalerTiming(0, period, 0);
    qDebug("set scaler settings to %lx", ret);
#endif
#endif
}

void mvmeControl::toggleDisplay(bool state)
{
/*    if(state){
        theApp->showData();
        displayButton->setText("hide display");
    }
    else{
        theApp->hideData();
        displayButton->setText("show display");
    }
*/}

void mvmeControl::dataTaking(bool val)
{
    bool ok, multi = false;
    QString str;
    unsigned int uperiod, words = 0;

    if(val){
        qDebug("Stop Data");
        theApp->stopDatataking();
        ui->cleardata->setEnabled(true);
    }
    else{
        qDebug("Start Data");
        // collect mode information
        str = ui->period->text();
        uperiod = str.toInt(&ok, 0);
        str = ui->readLength->text();
        words = str.toInt(&ok, 0);
        ui->cleardata->setEnabled(false);
        multi = ui->multiButton->isChecked();
        theApp->rd->clearData();
        theApp->rd->setFilter(ui->diagLowChannel2->value(), ui->diagHiChannel2->value());
        theApp->startDatataking(uperiod, multi, words, ui->MBLT->isChecked());
    }
}

void mvmeControl::clearData()
{
//    theApp->clearData();
}






void mvmeControl::selectListfile()
{
/*    QString name = QFileDialog::getSaveFileName("/home", "mesydaq data files (*.mdat);;all files (*.*);;really all files (*)", this, "Save as...");
    if(!name.isEmpty()){
        int i = name.find(".mdat");
        if(i == -1)
            name.append(".mdat");
        if(QFile::exists(name)){
            int answer = QMessageBox::warning(
                    this, "Listfile Exists -- Overwrite File",
            QString("Overwrite existing listfile?"),
            "&Yes", "&No", QString::null, 1, 1 );
            if(answer){
                name.sprintf(" ");
            }

        }
        listfilename->setText(name);
        theApp->setListfilename(name);
    }
*/}

void mvmeControl::listSlot()
{
/*    if(listbox->isChecked())
        theApp->listmodeOn(true);
    else
        theApp->listmodeOn(false);
*/
}

void mvmeControl::changeEndianess()
{
    qDebug("change endianess");
    if(ui->bigEndian->isChecked())
        theApp->vu->setEndianess(true);
    else
        theApp->vu->setEndianess(false);
}

void mvmeControl::replayListfile()
{
    //theApp->readListfile( );
}

void mvmeControl::writeData()
{
}


/*!
    \fn mvmeControl::saveData()
 */
void mvmeControl::saveData()
{
/*    QString s = QFileDialog::getSaveFileName(
            "/home",
    "Data files (*.txt)",
    this,
    "open file dialog",
    "Choose a file" );
    QFile f(s);
    if(!f.open(IO_ReadWrite))
        return;

    QTextStream t(&f);
    t << dataDisplay->text();
    f.flush();
    f.close();
*/}

void mvmeControl::checkSlot()
{
    theApp->dt->readData();
}

void mvmeControl::calcSlot()
{
    QString str;
    quint16 lobin = 0, hibin = 8192;
    // evaluate bin filter
    if(ui->bin1->isChecked()){
        lobin = ui->binRange1lo->value();
        hibin = ui->binRange1hi->value();
    }
    if(ui->bin2->isChecked()){
        lobin = ui->binRange2lo->value();
        hibin = ui->binRange2hi->value();
    }
    if(ui->bin3->isChecked()){
        lobin = ui->binRange3lo->value();
        hibin = ui->binRange3hi->value();
    }

    theApp->diag->calcAll(theApp->getHist(), ui->diagLowChannel2->value(), ui->diagHiChannel2->value(),
                          ui->diagLowChannel->value(), ui->diagHiChannel->value(), lobin, hibin);
    dispAll();
}

void mvmeControl::clearSlot()
{
    theApp->clearAllHist();
}

void mvmeControl::dispAll()
{
    dispDiag1();
    dispDiag2();
    dispResultlist();
}

void mvmeControl::dispDiag1()
{
    QString str;
    // upper range
    str.sprintf("%2.2f", theApp->diag->getMean(MAX));
    ui->meanmax->setText(str);
    str.sprintf("%d", theApp->diag->getMeanchannel(MAX));
    ui->meanmaxchan->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(MAX));
    ui->sigmax->setText(str);
    str.sprintf("%d", theApp->diag->getSigmachannel(MAX));
    ui->sigmaxchan->setText(str);
    str.sprintf("%2.2f", theApp->diag->getMean(MIN));
    ui->meanmin->setText(str);
    str.sprintf("%d", theApp->diag->getMeanchannel(MIN));
    ui->meanminchan->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(MIN));
    ui->sigmin->setText(str);
    str.sprintf("%d", theApp->diag->getSigmachannel(MIN));
    ui->sigminchan->setText(str);

    // odd even values upper range
    str.sprintf("%2.2f", theApp->diag->getMean(ODD));
    ui->meanodd->setText(str);
    str.sprintf("%2.2f", theApp->diag->getMean(EVEN));
    ui->meaneven->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(ODD));
    ui->sigmodd->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(EVEN));
    ui->sigmeven->setText(str);
}

void mvmeControl::dispDiag2()
{
    QString str;
    // lower range
    str.sprintf("%2.2f", theApp->diag->getMean(MAXFILT));
    ui->meanmax_filt->setText(str);
    str.sprintf("%d", theApp->diag->getMeanchannel(MAXFILT));
    ui->meanmaxchan_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(MAXFILT));
    ui->sigmax_filt->setText(str);
    str.sprintf("%d", theApp->diag->getSigmachannel(MAXFILT));
    ui->sigmaxchan_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getMean(MINFILT));
    ui->meanmin_filt->setText(str);
    str.sprintf("%d", theApp->diag->getMeanchannel(MINFILT));
    ui->meanminchan_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(MINFILT));
    ui->sigmin_filt->setText(str);
    str.sprintf("%d", theApp->diag->getSigmachannel(MINFILT));
    ui->sigminchan_filt->setText(str);

    // odd even values lower range
    str.sprintf("%2.2f", theApp->diag->getMean(ODDFILT));
    ui->meanodd_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getMean(EVENFILT));
    ui->meaneven_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(ODDFILT));
    ui->sigmodd_filt->setText(str);
    str.sprintf("%2.2f", theApp->diag->getSigma(EVENFILT));
    ui->sigmeven_filt->setText(str);
}

void mvmeControl::dispResultlist()
{
    QString text, str;
    quint16 i;

    for(i=0;i<34;i++){
        str.sprintf("%d:\t mean: %2.2f,\t sigma: %2.2f,\t\t counts: %d\n", i, theApp->diag->getMean(i),
                    theApp->diag->getSigma(i), theApp->diag->getCounts(i));
        text.append(str);
    }
    ui->diagResult->setPlainText(text);
}

void mvmeControl::dispRt()
{
    QString str;
    str.sprintf("%2.2f", theApp->rd->getRdMean(0));
    ui->rtMeanEven->setText(str);
    str.sprintf("%2.2f", theApp->rd->getRdMean(1));
    ui->rtMeanOdd->setText(str);
    str.sprintf("%2.2f", theApp->rd->getRdSigma(0));
    ui->rtSigmEven->setText(str);
    str.sprintf("%2.2f", theApp->rd->getRdSigma(1));
    ui->rtSigmOdd->setText(str);
}

void mvmeControl::dispChan(int c)
{
    qDebug("dispchan");
    QString str;
    str.sprintf("%d", theApp->diag->getChannel(theApp->getHist(), ui->diagChan->value(), ui->diagBin->value()));
    ui->diagCounts->setText(str);
}
