/*

Purpose of the mvme_listfile_reader program:

- Read mvme listfiles and pass readout data to handler code.
- Accept any type of mvme listfile types (MVMELST, MVLC_*) both as a flat file
  and within a ZIP archive.
- If multi event splitting is wanted load the filter strings from the standard
  mvme template system (vats).
- Load user specified code and invoke it with data extracted from the listfile.

mvme_listfile_reader
    input_listfile_filename0, input_listfile_filename1, ...

    --dump-data / --no-dump-data
    --plugin my_c_plugin.so,--foo=bar,-osomething
    --plugin my_qt_plugin.so

Notes:
- There is no need to specify the library extension when using
  QPluginLoader or QLibrary.

- C-style libraries need to be wrapped in an extern "C" block if compiled using
  a c++ compiler.

- make the dumper/printer code a plugin too and prepend it to the list of
  default plugins. Remove it if --no-print-data is specified.

- multi event splitting config is stored in the analysis right now. This
  functionality should be included in the mvme-read-listfile program.

  The problem is: need to get the filter string for multi event splitting for
  each module. This depends on the module type. If this was stored in the vme
  config at module creation time it would be easy to get.
  The other way to get the split filter is to use the template system and load
  the strings from there.
  Just use the template system for now but think about implementing the change
  to store the filter strings (and possibly all module meta) directly inside
  the vme config.

  Another concern: the multi event splitter could be a plugin in itself. This
  plugin would produce output just like the coding reading and parsing the
  listfile but it would yield N output events for each incoming event.
  Should plugins be given the possibilty to call output functions? This would
  be a bit more complicated than just consuming the data as a correct array of
  ModuleData structures would have to be filled.

  -> This looks more like GO4 than something small and compact and quick to
  use. Probably these things are outside the scope of this tool at least for
  the first ieration.

- How to handle system events? These do not really fit into the event.module
  scheme.

*/

#include <iostream>
#include <string>
#include <vector>
#include <QLibrary>

#include "listfile_reader.h"

using std::cout;
using std::endl;

struct RawDataPlugin
{
    PluginInfo info;
    PluginInit init;
    PluginDestroy destroy;
    BeginRun begin_run;
    EventData event_data;
    EndRun end_run;
};

struct resolve_error: public std::exception {};

template<typename Signature>
Signature resolve(QLibrary &lib, const char *func)
{
    if (auto result = (Signature)lib.resolve(func))
        return result;

    std::cout << "Error resolving function \"" << func << "\""
        << " from library " << lib.fileName().toStdString() << endl;

    throw resolve_error();
}

int main(int argc, char *argv[])
{
    std::vector<std::string> inputFilenames;
    std::vector<std::string> pluginSpecs;

    try
    {
        QLibrary pluginLib("./listfile_reader_print_plugin");

        if (!pluginLib.load())
        {
            cout << "Error loading plugin " << pluginLib.fileName().toStdString() << endl;
            return 1;
        }

        RawDataPlugin plugin = {};

        plugin.info = resolve<PluginInfo>(pluginLib, "plugin_info");
        plugin.init = resolve<PluginInit>(pluginLib, "plugin_init");
        plugin.destroy = resolve<PluginDestroy>(pluginLib, "plugin_destroy");
        plugin.begin_run = resolve<BeginRun>(pluginLib, "begin_run");
        plugin.event_data = resolve<EventData>(pluginLib, "event_data");
        plugin.end_run = resolve<EndRun>(pluginLib, "end_run");

        {
            char *plugin_name = {};
            char *plugin_descr = {};

            plugin.info(&plugin_name, &plugin_descr);

            cout << "Plugin Info from " << pluginLib.fileName().toStdString()
                << ": name=" << plugin_name
                << ", description=" << plugin_descr
                << endl;
        }
    }
    catch (const resolve_error &)
    {
        return 1;
    }

    return 0;
}
