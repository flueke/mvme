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

#if 0
# Sources
event0.module1.c0
event0.module1.c1
# mdpp16:
event1.mdpp16_0.c0 - c15        amplitudes (channel addresses 0-15)
event1.mdpp16_0.time0 - t15     time (channel addresses 16-31)
event1.mdpp16_0.trig0           (addr 32)
event1.mdpp16_0.trig1           (addr 33)
event0.module1.timestamp
event0.module1.length
event0.module1.eventcounter

# Histograms
Needed info: resolution, if scaling: max resolution of the value to properly scale it down
over/underflow handling -> extra bins counting over and underflows

Histo1D: One source
sourceX = event0.module0.c0

Histo2D: x and y sources
sourceX = event0.module0.c0
sourceY = event0.module0.c1

# Source to Histogram
histo name to data source path

# Implementation for now:
* Define Histo1D: name, resolution
* Set Histo1D source to the full channel path event.module.channel
* => Mapping of event.module.channel to Histo1D
*
* Define Histo2D: name, xres, yres
* Set Histo2D sourceX to full channel path
* Set Histo2D sourceY to full channel path
* => Mapping of event.module.channel to Histo2D.xAxis
* => Mapping of event.module.channel to Histo2D.yAxis

Once Histo2D has values for both x and y axis, it increments the corresponding cell

Events
    Modules

Histo1D = [
{ name, res }
...
]

Histo2D = [
{ name, resX, resY }
...
]

Histo1D mappings = [
{ channelpath, hist1d_name }
...
]

Histo2D mappings = [
{ hist2d_name, channelpathX, channelpathY }
...
]

Internally:
    channelPath -> (Hist1D)
    channelPath -> (Hist2D, axis)

#endif
