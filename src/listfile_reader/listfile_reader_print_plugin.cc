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
    cout << __PRETTY_FUNCTION__ << " - args:" << endl;

    for (int argi = 0; argi < argc; argi++)
    {
        cout << "  " << argv[argi] << endl;
    }

    return nullptr;
}

void plugin_destroy (void *userptr)
{
    cout << __PRETTY_FUNCTION__ << " - userptr=" << userptr;
}

void begin_run (void *userptr, const RunDescription *run)
{
    cout << __PRETTY_FUNCTION__;
}

void event_data (void *userptr, int eventIndex, const ModuleData *modules, int moduleCount)
{
    cout << __PRETTY_FUNCTION__;
}
void end_run (void *userptr)
{
    cout << __PRETTY_FUNCTION__;
}

}
