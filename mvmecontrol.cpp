#include "mvmecontrol.h"
#include "ui_mvmecontrol.h"
#include "mvme.h"
#include "datathread.h"
#include "diagnostics.h"
#include "realtimedata.h"
#include "CVMUSBReadoutList.h"

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
#include <QDebug>
#include <QCoreApplication>

static const char *defaultStackFile = "default-stack.stk";
static const char *defaultInitListFile  = "default-initlist.init";

mvmeControl::mvmeControl(mvme *theApp, QWidget *parent) :
    QWidget(parent),
    theApp(theApp),
    ui(new Ui::mvmeControl)
{
    dontUpdate = true;

    ui->setupUi(this);

    connect(theApp->vu, SIGNAL(daqModeChanged(bool)),
            this, SLOT(vmusbDaqModeChanged(bool)));

    QString pathToBinary = QFileInfo(QCoreApplication::applicationFilePath()).dir().path();

    {
        QString filename = QFileInfo(pathToBinary, defaultStackFile).filePath();
        QFile inFile(filename);
        if (inFile.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&inFile);
            ui->stackInput->setText(stream.readAll());
        }
    }

    {
        QString filename = QFileInfo(pathToBinary, defaultInitListFile).filePath();
        QFile inFile(filename);
        if (inFile.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&inFile);
            ui->memoryInput_2->setText(stream.readAll());
        }
    }

    ui->memoryInput->setText(ui->memoryInput_2->toPlainText());

    dontUpdate = false;

    connect(ui->pb_readRegister, &QPushButton::clicked, this, &mvmeControl::readRegister);
    connect(ui->spin_RegDec, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), ui->spin_RegHex, &QSpinBox::setValue);
    connect(ui->spin_RegHex, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), ui->spin_RegDec, &QSpinBox::setValue);
}

mvmeControl::~mvmeControl()
{
    delete ui;
}

void mvmeControl::vmusbDaqModeChanged(bool daqModeOn)
{
    ui->pb_readMemory_2->setEnabled(!daqModeOn);
    ui->cb_useDaqMode->setEnabled(!daqModeOn);
    ui->gb_fileIO->setEnabled(!daqModeOn);
    ui->vm_usb->setEnabled(!daqModeOn);
    ui->Init->setEnabled(!daqModeOn);
    ui->Diagnostics->setEnabled(!daqModeOn);
    ui->Debug->setEnabled(!daqModeOn);
}

void mvmeControl::getValues()
{
#ifdef VME_CONTROLLER_WIENER
    if (theApp->vu->isOpen())
    {
       theApp->vu->readAllRegisters();
       refreshDisplay();
    }
#endif

}

void mvmeControl::changeMode()
{
#ifdef VME_CONTROLLER_WIENER

    qDebug() << "changeMode(): dontUpdate =" << dontUpdate << ", sender =" << sender();


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
    qDebug() << "begin refreshDisplay()";
#ifdef VME_CONTROLLER_WIENER
    int val, val2;
    QString str;

    dontUpdate = true;

    str.sprintf("%x",theApp->vu->getFirmwareId());
    ui->firmwareLabel->setText(str);

    // Global Mode
    val = theApp->vu->getMode();

    qDebug("refreshDisplay: mode=%08x", val);

    if (val < 0) return;

    // Buffer Options
    val2 = val & 0x0F;
    ui->bufSize->setCurrentIndex(val2);
    val2 = val & 0x20;
    if(val2)
        ui->mixedBuffers->setChecked(true);
    else
        ui->mixedBuffers->setChecked(false);

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

  QString irqText;

  for (int i=0; i<8; ++i)
  {
      uint16_t irqValue = theApp->vu->getIrq(i);
      QString buffer;

      buffer.sprintf("Vector %d = 0x%04x\n", i, irqValue);
      irqText.append(buffer);
  }

  ui->irqVectorsLabel->setText(irqText);

  u32 usbSetup = static_cast<u32>(theApp->vu->getUsbSettings());
  u32 numberOfBuffers = (usbSetup & TransferSetupRegister::multiBufferCountMask);
  u32 timeout = (usbSetup & TransferSetupRegister::timeoutMask) >> TransferSetupRegister::timeoutShift;

  qDebug("usb bulk setup: %08x, buffers=%u, timeout=%u",
          usbSetup, numberOfBuffers, timeout);

  ui->usbBulkBuffers->setValue(numberOfBuffers);
  ui->usbBulkTimeout->setValue(timeout);

  u32 daqSettings = theApp->vu->getDaqSettings();
  u32 triggerDelay = (daqSettings & DaqSettingsRegister::ReadoutTriggerDelayMask) >> DaqSettingsRegister::ReadoutTriggerDelayShift;
  u32 readoutFrequency = (daqSettings & DaqSettingsRegister::ScalerReadoutFrequencyMask) >> DaqSettingsRegister::ScalerReadoutFrequencyShift;
  u32 readoutPeriod = (daqSettings & DaqSettingsRegister::ScalerReadoutPerdiodMask) >> DaqSettingsRegister::ScalerReadoutPerdiodShift;

  qDebug("daqSettings: %08x, triggerDelay=%u, readoutFrequency=%u, readoutPeriod=%u",
         daqSettings, triggerDelay, readoutFrequency, readoutPeriod);

  ui->spin_triggerDelay->setValue(triggerDelay);
  ui->spin_readoutFrequency->setValue(readoutFrequency);
  ui->spin_readoutPeriod->setValue(readoutPeriod);

  dontUpdate = false;
#endif
  qDebug() << "end refreshDisplay()";
}

void mvmeControl::readVme()
{
    bool ok;
    QString str, str2;
    long ret = 0;
    quint32 data[4096];
    str = ui->readOffset->text();
    long offset = str.toInt(&ok, 0);
    str = ui->readAddr->text();
    long addr = str.toInt(&ok, 0) + (0x10000 * offset);

    if(ui->readBlt->isChecked() || ui->readMblt->isChecked()){
        unsigned char size;
        str = ui->readBltsize->text();
        size = str.toUInt(&ok, 0);
        if(ui->readBlt->isChecked()){
            ret = theApp->vu->vmeBltRead32(addr, size, data);
            qDebug("read %ld bytes/%ld words from bus by BLT (requested %ld words)",
                   ret, ret/sizeof(uint32_t), size);
        }
        else{
            ret = theApp->vu->vmeMbltRead32(addr, size, data);
            qDebug("read %ld bytes from bus by MBLT", ret);
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
        {
            ret = theApp->vu->vmeRead16(addr, (long*)data);
            str.sprintf("%04lx", data[0]);
        }
        else
        {
            ret = theApp->vu->vmeRead32(addr, (long*)data);
            str.sprintf("%08lx", data[0]);
        }

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
#if 0
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
#endif
}

void mvmeControl::activateStack()
{
#if 0
    QString s, s2;
    const uint stackMaxSize = 0x10000;
    long stack[stackMaxSize];
    s=ui->stackDisplay->toPlainText();
    QTextStream t(&s, QIODevice::ReadWrite);

    if(ui->stackNumber->currentText() == "Memory")
    {
        bool ok;

        long memoryOffset = ui->memoryOffset->text().toInt(&ok, 0);
        memoryOffset <<= 16;

        qDebug("activateStack: memoryOffset=%lx, ok=%d", memoryOffset, ok);

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
            stack[i] += memoryOffset;
            int writeResult = theApp->vu->vmeWrite16(stack[i], stack[i+1]);
            i++;
            qDebug("wrote %lx to %lx, write result=%d", stack[i], stack[i-1], writeResult);
        }
    } else
    {
#if 1
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
            qDebug("stackExecute returned %d", ret);

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
#else
    std::vector<uint32_t> stackData;
    while (!t.atEnd())
    {
        uint32_t value = 0;
        t >> value;

        if (t.status() != QTextStream::Ok)
            break;
        stackData.push_back(value);
    }

    CVMUSBReadoutList stackList(stackData);

    if (ui->stackNumber->currentText() == "Execute")
    {
        uint32_t readBuffer[1024];
        size_t bytesRead = 0;
        int result = theApp->vu->listExecute(&stackList, readBuffer, 1024*4, &bytesRead);
        qDebug("stackExecute: result=%d, bytesRead=%lu", result, bytesRead);

        for (size_t i=0; i<bytesRead/4; ++i)
        {
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
        int ret = theApp->vu->listLoad(&stackList, val, 0x0);
        qDebug("stackWrite: returned %d", ret);
    }
}
#endif
#endif
}

void mvmeControl::loadStack()
{
#if 0
   QString s = QFileDialog::getOpenFileName(this,
                                            "Choose a file",
                                            "/home",
                                            "Stack files (*.stk *.txt)"
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
#endif
}

void mvmeControl::loadData()
{
#if 0
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
#if 0
    bool ok;
    long stack[0x1000];
    QString s, s2;
    int val = ui->stackNumber->currentText().toInt(&ok, 0);
    int ret = theApp->vu->stackRead(val, &stack[0]);
    qDebug("read %d bytes from stack %d", ret, val);

    for(int i = 0; i < ret/2; i++){
        s2.sprintf("0x%08lx",stack[i]);
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

    refreshDisplay();
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
#ifdef VME_CONTROLLER_WIENER
    if(val){
        theApp->vu->writeActionRegister(0);
        qDebug("Stop: 0");
        int ret;
        do {
            qDebug("performing usb read to clear remaning data");
            char buffer[27 * 1024];
            ret = usb_bulk_read(theApp->vu->hUsbDevice, XXUSB_ENDPOINT_IN, buffer, 27 * 1024, 500);
            qDebug("bulk read returned %d", ret);
        } while (ret > 0);
    }
    else{
        theApp->vu->writeActionRegister(1);
        qDebug("Start: 1");
    }
#endif
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

void mvmeControl::dataTaking(bool stop)
{
    bool ok, multi = false;
    QString str;
    unsigned int uperiod, words = 0;

    if(stop)
    {
        qDebug("Stop Datataking");
        theApp->stopDatataking();
        ui->cleardata->setEnabled(true);
    }
    else
    {
        qDebug("Start Datataking");
        ui->cleardata->setEnabled(false);

        theApp->rd->clearData();
        theApp->rd->setFilter(ui->diagLowChannel2->value(), ui->diagHiChannel2->value());


        /* XXX, FIXME, TODO:
         * Set irqs using the values currently entered into the vm_usb irq tab,
         * perform a stack write as if the corresponding button was clicked and
         * transfer the init list data shown on the DataOp tab to the device.
         * With suitable default values in the textedits this results in a
         * working "one click" setup. */
        setIrq();
        on_pb_stackLoad_clicked();
        on_pb_writeMemory_2_clicked();

        theApp->startDatataking(0, false, 0, false, ui->cb_useDaqMode->isChecked());
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

void mvmeControl::readRegister()
{
    u32 address = static_cast<u32>(ui->spin_RegDec->value());
    u32 value = 0;
    if (theApp->vu->readRegister(address, &value))
    {
        QString buffer;
        buffer.sprintf("0x%08x", value);
        ui->le_readRegisterResult->setText(buffer);
    }
    else
    {
        ui->le_readRegisterResult->setText(QSL("Error reading register"));
    }
}

void mvmeControl::on_pb_clearRegisters_clicked()
{
    theApp->vu->writeActionRegister(4);
    getValues();
}

void mvmeControl::on_usbBulkBuffers_valueChanged(int value)
{
    if (dontUpdate) return;

    int usbSetup = theApp->vu->getUsbSettings();
    value &= TransferSetupRegister::multiBufferCountMask;
    usbSetup &= ~TransferSetupRegister::multiBufferCountMask;
    usbSetup |= value;

    theApp->vu->setUsbSettings(usbSetup);
    refreshDisplay();
}

void mvmeControl::on_usbBulkTimeout_valueChanged(int value)
{
    if (dontUpdate) return;

    int usbSetup = theApp->vu->getUsbSettings();
    value = (value << TransferSetupRegister::timeoutShift) & TransferSetupRegister::timeoutMask;
    usbSetup &= ~TransferSetupRegister::timeoutMask;
    usbSetup |= value;

    theApp->vu->setUsbSettings(usbSetup);
    refreshDisplay();
}

void mvmeControl::on_spin_triggerDelay_valueChanged(int value)
{
    u32 regValue = theApp->vu->getDaqSettings();
    regValue &= ~DaqSettingsRegister::ReadoutTriggerDelayMask;
    regValue |= (value << DaqSettingsRegister::ReadoutTriggerDelayShift) & DaqSettingsRegister::ReadoutTriggerDelayMask;
    theApp->vu->setDaqSettings(regValue);
}

void mvmeControl::on_spin_readoutFrequency_valueChanged(int value)
{
    u32 regValue = theApp->vu->getDaqSettings();
    regValue &= ~DaqSettingsRegister::ScalerReadoutFrequencyMask;
    regValue |= (value << DaqSettingsRegister::ScalerReadoutFrequencyShift) & DaqSettingsRegister::ScalerReadoutFrequencyMask;
    theApp->vu->setDaqSettings(regValue);
}

void mvmeControl::on_spin_readoutPeriod_valueChanged(int value)
{
    u32 regValue = theApp->vu->getDaqSettings();
    regValue &= ~DaqSettingsRegister::ScalerReadoutPerdiodMask;
    regValue |= (value << DaqSettingsRegister::ScalerReadoutPerdiodShift) & DaqSettingsRegister::ScalerReadoutPerdiodMask;
    theApp->vu->setDaqSettings(regValue);
}

void mvmeControl::on_pb_selectOutputFile_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Choose a file", QString(), "Mesytec Data Files (*.mdat);;All Files (*)");

    if (!fileName.endsWith(".mdat"))
        fileName += ".mdat";

    ui->le_outputFile->setText(fileName);
    ui->cb_outputFileEnabled->setChecked(!fileName.isNull());
}

void mvmeControl::on_pb_selectInputFile_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Choose a file", QString(), "Mesytec Data Files (*.mdat)");
    ui->le_inputFile->setText(fileName);
    ui->cb_inputFileEnabled->setChecked(!fileName.isNull());
}

QString mvmeControl::getOutputFileName() const
{
    if (ui->cb_outputFileEnabled->isChecked())
        return ui->le_outputFile->text();
    return QString();
}

QString mvmeControl::getInputFileName() const
{
    if (ui->cb_inputFileEnabled->isChecked())
        return ui->le_inputFile->text();
    return QString();
}

void mvmeControl::on_pb_stackLoad_clicked()
{
    QString str = ui->stackInput->toPlainText();
    QTextStream stream(&str);
    auto stackData = parseStackFile(stream);

    if (stackData.size())
    {
        u8  stackNumber = ui->stackNumber->currentIndex();
        u32 loadOffset = ui->stackLoadOffset->value();
        theApp->vu->stackWrite(stackNumber, loadOffset, stackData);
    }
}

void mvmeControl::on_pb_stackRead_clicked()
{
    u8  stackNumber = ui->stackNumber->currentIndex();
    auto result = theApp->vu->stackRead(stackNumber);

    QString buffer;

    if (result.first.size())
    {

        buffer.append(QString().sprintf("# load offset=0x%04x\n", result.second));

        for (auto value: result.first)
        {
            buffer.append(QString().sprintf("0x%08x\n", value));
        }
    }
    ui->stackOutput->setText(buffer);
}

void mvmeControl::on_pb_stackExecute_clicked()
{
    QString str = ui->stackInput->toPlainText();
    QTextStream stream(&str);
    auto stackData = parseStackFile(stream);

    if (stackData.size())
    {
        const size_t readBufferSize = 27 * 1024;
        char readBuffer[readBufferSize];
        size_t bytesRead;

        CVMUSBReadoutList stackList(stackData);
        theApp->vu->listExecute(&stackList, readBuffer, readBufferSize, &bytesRead);

        size_t longWords = bytesRead / sizeof(u32);
        size_t bytesRemaining = bytesRead % sizeof(u32);
        size_t shortsRemaining = bytesRemaining / sizeof(u16);
        QString buffer;

        for (size_t i=0; i<longWords; ++i)
        {
            u32 value = (reinterpret_cast<u32 *>(readBuffer))[i];
            buffer.append(QString().sprintf("0x%08x\n", value));
        }

        for (size_t i=0; i<shortsRemaining; ++i)
        {
            size_t offset = longWords * sizeof(u32) + i;
            u16 value = *reinterpret_cast<u16 *>(readBuffer + offset);
            buffer.append(QString().sprintf("0x%04x\n", value));
        }

#if 0
        u32 *bufp = reinterpret_cast<u16 *>(readBuffer);
        u32 *endp = reinterpret_cast<u16 *>(readBuffer + bytesRead);

        QString buffer, temp;

        for (; bufp < endp; ++bufp)
        {
            temp.sprintf("0x%08x\n", *bufp);
            buffer.append(temp);
        }
#endif

        ui->stackOutput->setText(buffer);
    }
}

void mvmeControl::on_pb_stackFileLoad_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Choose a file", QString(), "Stack Files (*.stk);;All Files (*)");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QTextStream stream(&file);
    ui->stackInput->setText(stream.readAll());
}

void mvmeControl::on_pb_stackFileSave_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Choose a file", QString(), "Stack Files (*.stk)");

    if (!fileName.endsWith(".stk"))
        fileName += ".stk";

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QTextStream stream(&file);

    stream << ui->stackInput->toPlainText();
}

// =========================================

void mvmeControl::on_pb_readMemory_clicked()
{
    bool ok;
    u32 memoryOffset = ui->memoryOffset->text().toInt(&ok, 0);
    memoryOffset <<= 16;

    QString str = ui->memoryInput->toPlainText();
    QTextStream stream(&str);
    auto memoryData = parseStackFile(stream);
    QString buffer;

    for (int i=0; i<memoryData.size() / 2; ++i)
    {
        u32 address = memoryOffset | memoryData[2*i];
        long value = 0;
        int result = theApp->vu->vmeRead16(address, &value);

        qDebug("read 0x%08x -> 0x%04x, result=%d", address, value, result);

        if (result > 0)
        {
            buffer.append(QString().sprintf("0x%04x 0x%04x\n", address & 0xFFFF, value));
        }
    }

    ui->memoryOutput->setText(buffer);
}

void mvmeControl::on_pb_writeMemory_clicked()
{
    bool ok;
    u32 memoryOffset = ui->memoryOffset->text().toInt(&ok, 0);
    memoryOffset <<= 16;
    QString str = ui->memoryInput->toPlainText();
    QTextStream stream(&str);
    auto memoryData = parseStackFile(stream);
    QString buffer;

    for (int i=0; i<memoryData.size() / 2; ++i)
    {
        u32 address = memoryOffset | memoryData[2*i];
        u16 value   = memoryData[2*i+1];
        int result  = theApp->vu->vmeWrite16(address, value);

        qDebug("write  0x%08x -> 0x%04x, result=%d", address, value, result);

        if (result < 0)
        {
            buffer.append(QString().sprintf("Error setting 0x%08x to 0x%04x: %d\n",
                        address, value, result));
        }
    }

    ui->memoryOutput->setText(QString(buffer));
}

void mvmeControl::on_pb_loadMemoryFile_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Choose a file", QString(), "Init Lists (*.init *.stk);;All Files (*)");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QTextStream stream(&file);
    ui->memoryInput->setText(stream.readAll());
}

void mvmeControl::on_pb_saveMemoryFile_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Choose a file", QString(), "Init Lists (*.init)");

    if (!fileName.endsWith(".init"))
        fileName += ".init";

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QTextStream stream(&file);

    stream << ui->memoryInput->toPlainText();
}

// =========================================

void mvmeControl::on_pb_readMemory_2_clicked()
{
    bool ok;
    u32 memoryOffset = ui->memoryOffset_2->text().toInt(&ok, 0);
    memoryOffset <<= 16;

    QString str = ui->memoryInput_2->toPlainText();
    QTextStream stream(&str);
    auto memoryData = parseStackFile(stream);
    QString buffer;

    for (int i=0; i<memoryData.size() / 2; ++i)
    {
        u32 address = memoryOffset | memoryData[2*i];
        long value = 0;
        int result = theApp->vu->vmeRead16(address, &value);

        qDebug("read 0x%08x -> 0x%04x, result=%d", address, value, result);

        if (result > 0)
        {
            buffer.append(QString().sprintf("0x%04x 0x%04x\n", address & 0xFFFF, value));
        }
    }

    ui->memoryOutput_2->setText(buffer);
}

void mvmeControl::on_pb_writeMemory_2_clicked()
{
    /* XXX, FIXME, TODO: horrible hack to make the "write" button work in daq mode. */


    bool wasInDaqMode = false;

    if (theApp->vu->isInDaqMode())
    {
        wasInDaqMode = true;
        dataTaking(true);
    }


    bool ok;
    u32 memoryOffset = ui->memoryOffset_2->text().toInt(&ok, 0);
    memoryOffset <<= 16;
    QString str = ui->memoryInput_2->toPlainText();
    QTextStream stream(&str);
    auto memoryData = parseStackFile(stream);
    QString buffer;

    for (int i=0; i<memoryData.size() / 2; ++i)
    {
        u32 address = memoryOffset | memoryData[2*i];
        u16 value   = memoryData[2*i+1];
        int result  = theApp->vu->vmeWrite16(address, value);

        qDebug("write  0x%08x -> 0x%04x, result=%d", address, value, result);

        if (result < 0)
        {
            buffer.append(QString().sprintf("Error setting 0x%08x to 0x%04x: %d\n",
                        address, value, result));
        }
    }

    ui->memoryOutput_2->setText(QString(buffer));

    if (wasInDaqMode)
    {
        dataTaking(false);
    }
}

void mvmeControl::on_pb_loadMemoryFile_2_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Choose a file", QString(), "Init Lists (*.init *.stk);;All Files (*)");

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QTextStream stream(&file);
    ui->memoryInput_2->setText(stream.readAll());
}

void mvmeControl::on_pb_saveMemoryFile_2_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Choose a file", QString(), "Init Lists (*.init)");

    if (!fileName.endsWith(".init"))
        fileName += ".init";

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QTextStream stream(&file);

    stream << ui->memoryInput_2->toPlainText();
}

void mvmeControl::on_pb_usbReset_clicked()
{
    theApp->vu->writeActionRegister(4);
}

void mvmeControl::on_pb_errorRecovery_clicked()
{
    int bytesRead = 0;
    do
    {
        char buffer[27 * 1024];
        bytesRead = theApp->vu->bulkRead(buffer, sizeof(buffer));
        qDebug("bulk read returned %d", bytesRead);
    } while (bytesRead > 0);
}
