#include "listfile_reader.h"
#include <iostream>

using std::cout;
using std::endl;

static const char *PluginName = "Print Plugin";
static const char *PluginDescription = "Prints raw readout module data";

extern "C"
{

void plugin_info (const char **plugin_name, const char **plugin_description)
{
    *plugin_name = PluginName;
    *plugin_description = PluginDescription;
}

void *plugin_init (const char *pluginFilename, int argc, const char *argv[])
{
    cout << __PRETTY_FUNCTION__ << "plugin: " << pluginFilename << ",  args:" << endl;

    for (int argi = 0; argi < argc; argi++)
    {
        cout << "  " << argv[argi] << endl;
    }

    return nullptr;
}

void plugin_destroy (void *userptr)
{
    cout << __PRETTY_FUNCTION__ << " - userptr=" << userptr << endl;
}

void begin_run (void *userptr, const RunDescription *run)
{
    (void) userptr;
    (void) run;
    cout << __PRETTY_FUNCTION__ << endl;
}

static size_t g_eventsProcessed = 0;;
static const size_t EventPrintInterval = 1;

void event_data (void *userptr, int eventIndex, const ModuleData *modules, int moduleCount)
{
    if ((g_eventsProcessed++ % EventPrintInterval) == 0)
    {
        cout << __PRETTY_FUNCTION__ << " userptr=" << userptr << ", eventIndex=" <<
            eventIndex << ", moduleCount=" << moduleCount << endl;

        for (int mi = 0; mi < moduleCount; ++mi)
        {
            const ModuleData &md(modules[mi]);

            if (md.prefix.size)
            {
                cout << "  moduleIndex=" << mi << ", prefix.size=" << md.prefix.size << endl;
            }

            if (md.dynamic.size)
            {
                cout << "  moduleIndex=" << mi << ", dynamic.size=" << md.dynamic.size << endl;
            }

            if (md.suffix.size)
            {
                cout << "  moduleIndex=" << mi << ", suffix.size=" << md.suffix.size << endl;
            }

        }
    }
}
void end_run (void *userptr)
{
    (void) userptr;
    cout << __PRETTY_FUNCTION__ << endl;
}

}
