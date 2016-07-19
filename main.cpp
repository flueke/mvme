#include <QApplication>
#include "mvme.h"
#include "util.h"
#include <QDebug>
#include <QLibraryInfo>

#include "vmemodule.h"
#include "vmusb_stack.h"

void bulk_read(vmUsb *vmusb)
{
    char readBuffer[64 * 1024];
    int status = vmusb->bulk_read(readBuffer, sizeof(readBuffer));
    qDebug("bulk_read: %d", status);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion("0.2.0");

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);

#if 0
    mvme w;
    w.show();
#endif

    auto mdpp16 = new MDPP16(0x00000000, 0x01);
    mdpp16->setIrqLevel(1);
    mdpp16->setIrqVector(0);
    mdpp16->registerData[0x6070] = 3;
    mdpp16->registerData[0x6072] = 1000;

    auto madc32 = new MADC32(0x43210000, 0x02);
    //madc32->registerData[0x6070] = 7;

    auto stack = new VMUSB_Stack;

    stack->addModule(mdpp16);
    stack->addModule(madc32);

    stack->setTriggerType(VMUSB_Stack::Interrupt);
    stack->setStackID(2);
    stack->irqLevel = 1;
    stack->irqVector = 0;

    VMECommandList initCommands, readoutCommands, startDaqCommands, stopDaqCommands;

    stack->addInitCommands(&initCommands);
    stack->addReadoutCommands(&readoutCommands);
    stack->addStartDaqCommands(&startDaqCommands);
    stack->addStopDaqCommands(&stopDaqCommands);

    QTextStream out(stdout);

    out << "===== init commands" << endl;
    initCommands.dump(out);

    out << "===== readout commands" << endl;
    readoutCommands.dump(out);

    out << "===== startDaq commands" << endl;
    startDaqCommands.dump(out);

    out << "===== stopDaq commands" << endl;
    stopDaqCommands.dump(out);

    out << endl;

    auto vmusb = new vmUsb();
    vmusb->openUsbDevice();

    stack->loadStack(vmusb);
    stack->enableStack(vmusb);

    auto readResult = vmusb->stackRead(stack->getStackID());

    qDebug() << "stack read result:";
    for (uint32_t stackWord: readResult.first)
    {
        qDebug("  0x%08x", stackWord);
    }
    qDebug() << endl;

    char readBuffer[1024];

    stack->resetModule(vmusb);
    vmusb->executeCommands(&initCommands, readBuffer, sizeof(readBuffer));
    vmusb->executeCommands(&startDaqCommands, readBuffer, sizeof(readBuffer));

    vmusb->enterDaqMode();

    const size_t transferLimit = 10;

    for (size_t transfers = 0; transfers < transferLimit; ++transfers)
    {
        bulk_read(vmusb);
    }

    qDebug() << "stopDaq";
    vmusb->leaveDaqMode();
    bulk_read(vmusb);

    vmusb->executeCommands(&stopDaqCommands, readBuffer, sizeof(readBuffer));

    return 0;
    //return a.exec();
}
