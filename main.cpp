/* TODOs and NOTES:
 *
 * - Buffer passing between threads via queued signals/slots seems way too
 *   slow. Try a mutex + waitcondition solution instead.
 *   Note: using a buffer for multiple MVME events makes this fast enough...
 *
 * - Look for a text editor widget with syntax highlighting -> qt has this builtin
 *
 * - Replay mode: load reply file, disable modifications to settings, replay
 *   data possibly multiple times
 * - DAQStats needed! Log buffers with errors!
 * - Enable creating and filling of 2d histos
 */
#include "mvme.h"
#include "util.h"
#include "vmusb_stack.h"
#include "mvme_context.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<DAQState>("GlobalMode");
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
