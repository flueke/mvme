/* TODOs and NOTES:
 *
 * - Create 1d histograms before datataking time
 * - Save 1d and 2d histograms definitions in the config and externally in a histogram
 *   config (this is needed for listfile mode)
 * - Replay mode: disable modifications to settings
 * - Look for a text editor widget with syntax highlighting -> qt has this builtin
 */
#include "mvme.h"
#include "util.h"
#include "vmusb_stack.h"
#include "mvme_context.h"
#include "vmecontroller.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<DAQState>("GlobalMode");
    qRegisterMetaType<DAQState>("ControllerState");
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
    w.restoreSettings();

    return a.exec();
}
