#include <QApplication>
#include "mvme.h"
#include "util.h"
#include <QDebug>
#include <QLibraryInfo>

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

    mvme w;
    w.show();
    
    return a.exec();
}

void foo()
{

#if 1
    auto mdpp16 = new MDPP16(0x0);
    mdpp16->setRegister(0x6030, 1);
    mdpp16->loadInitList("mdpp16.init");
    mdpp16->setIrqPriority(1); // 1-7, 0=disable
    mdpp16->setIrqVector(0);  // 0-255
    // ....

    auto madc32 = new MADC32(0x1);
    madc32->setRegister(0x6030, 1);

    auto controller = new vmUsb();
    controller->open();

    QVector<VMEModule *> modules;
    modules << mdpp16 << madc32;

    auto stack = new VMUSBStack;
    stack->addModule(mdpp16);
    stack->addModule(madc32);
    // same as for mdpp16
    stack->setIrqPriority(1);
    stack->setIrqVector(0);

    //for (auto module: modules)
    //{
    //    // module knows init order and can use basic vme operations to init itself (how to make use of readout lists?)
    //    module->init(controller);
    //    irq = module->getIrq();
    //    module->addReadoutCommands(readoutLists[irq-1]);
    //}

    // load list to stack id 0, mem offset 0
    controller->listLoad(&readoutList, 0, 0, 200);
    controller->set

    for (auto module: modules)
    {
        // module knows what to do when starting datataking
        module->startDatataking(controller);
    }

    controller->startDaq();
    while (true)
    {
        controller->daqRead();
    }
    controller->stopDaq();
#endif
}
