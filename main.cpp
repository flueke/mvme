/* TODOs and NOTES:
 *
 * - Buffer passing between threads via queued signals/slots seems way too
 *   slow. Try a mutex + waitcondition solution instead.
 * - VME Module definitions should work with "init" lists all the way instead
 *   of just for the initialization part. The init part should be split into
 *   "physics parameter init" and "module daq init".
 *   All those init lists need to be stored in files and loaded with the daq
 *   config.
 * - Look for a text editor widget with syntax highlighting
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

    mvme w;
    w.show();

    return a.exec();
}
