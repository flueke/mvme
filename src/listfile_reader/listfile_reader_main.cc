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

    --multi-event-splitting / --no-multi-event-splitting
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
    QString filename;
    PluginInfo info;
    PluginInit init;
    PluginDestroy destroy;
    BeginRun begin_run;
    EventData event_data;
    EndRun end_run;
};

struct library_load_error: public std::exception {};
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

//
// Load a single plugin
//
RawDataPlugin load_plugin(const QString &name)
{
    QLibrary pluginLib("./listfile_reader_print_plugin");

    if (!pluginLib.load())
    {
        cout << "Error loading plugin " << pluginLib.fileName().toStdString()
            << endl;
        throw library_load_error();
    }

    RawDataPlugin plugin = {};

    plugin.filename = pluginLib.fileName();
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

        cout << "Loaded plugin from " << pluginLib.fileName().toStdString()
            << ": name=" << plugin_name
            << ", description=" << plugin_descr
            << endl;
    }

    return plugin;
}

//
// process_listfile
//
void process_one_listfile(const QString &filename)
{
    //auto replayHandle = open_listfile(filename);


    // One side could be a ListfileReplayWorker which is moved to its own
    // thread. This is the buffer producer side

    // In the middle is a buffer queue of 2 or more buffers.

    // The consumer could be this main thread.
    //
    // For MVLC consumer code could look like this:
    // Setup ReadoutParserCallbacks for the readout parser and optionally setup
    // a multi event splitter. Get the splitter filter strings from the
    // VATS.
    // Create a readout parser using the VMEConfig.
    // Call the parser for each buffer in the filled queue:
    //      pr = parse_readout_buffer_eth(
    //          m_parser, m_parserCallbacks,
    //          buffer->id, buffer->data, buffer->used);
    // or
    //      pr = parse_readout_buffer_usb(
    //          m_parser, m_parserCallbacks,
    //          buffer->id, buffer->data, buffer->used);
    //
    // MVMELST consumer:
    // This could be done by implementing a IMVMEStreamModuleConsumer and
    // attaching this to an instance of MVMEStreamWorker.
    // This would also work for the MVLC side as the MVLC_StreamWorker supports.
}

int main(int argc, char *argv[])
{
    std::vector<QString> inputFilenames;
    std::vector<QString> pluginSpecs;
    std::vector<RawDataPlugin> plugins;

    try
    {
        auto plugin = load_plugin("./listfile_reader_print_plugin");
        plugins.emplace_back(plugin);
    }
    catch (const resolve_error &)
    {
        return 1;
    }

    if (plugins.empty())
    {
        cout << "Error: no plugins could be loaded" << endl;
        return 1;
    }

    // For each listfile:
    //   open the file for reading (zip/non-zip should work)
    //   read the vme config from the file, get the vme controller type, create the factory
    for (const auto &listfileFilename: inputFilenames)
    {
        try
        {
            process_one_listfile(listfileFilename);
        }
        catch (const std::exception &e)
        {
            cout << "Error processing file: " << listfileFilename.toStdString()
                << ": " << e.what() << endl;
        }
        catch (const QString &msg)
        {
            cout << "Error processing file: " << listfileFilename.toStdString()
                << ": " << msg.toStdString() << endl;
        }
    }

    return 0;
}
