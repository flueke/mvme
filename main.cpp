/* TODOs and NOTES:
 *
 * To make this work asap:
 * make a context object holding
 * - vme controller
 * - modules
 * - chains
 * - stacks
 *
 * create guis to be able to
 * - add new modules and configure them
 * - create new stacks and add modules/chains to them
 * - create new chains and add modules to them
 * - module reset
 * - module init
 * - module start daq
 * - module stop daq
 * - module readout code
 * - remove modules, stacks, chains!
 *
 * daq gui:
 * - start / stop daq
 * - init only
 * - run for n cycles then stop
 *
 * raw data explorer
 * - parse controller headers and display info
 * - show module info
 * - display data as longwords
 */
#include "mvme.h"
#include "util.h"
#include "vme_module.h"
#include "vmusb_stack.h"
#include "mvme_context.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>


void bulkRead(VMUSB *vmusb)
{
    char readBuffer[64 * 1024];
    int status = vmusb->bulkRead(readBuffer, sizeof(readBuffer));
    qDebug("bulkRead: %d", status);
}

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion("0.2.0");

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);

#if 1
    mvme w;
    w.show();

    return a.exec();
#else
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
        bulkRead(vmusb);
    }

    qDebug() << "stopDaq";
    vmusb->leaveDaqMode();
    qDebug() << "drain vmusb";
    bulkRead(vmusb);

    vmusb->executeCommands(&stopDaqCommands, readBuffer, sizeof(readBuffer));

    return 0;
#endif
}
