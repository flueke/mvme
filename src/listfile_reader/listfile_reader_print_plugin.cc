/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
    cout << "listfileFilename: " << run->listfileFilename << endl
        << "eventCount: " << run->eventCount << endl;

    for (int ei=0; ei<run->eventCount; ++ei)
    {
        auto &eventDescr = run->events[ei];

        cout << "  event" << ei << ": name=" << eventDescr.name
            << ", moduleCount=" << eventDescr.moduleCount << endl;

        for (int mi=0; mi<eventDescr.moduleCount; ++mi)
        {
            auto &module = eventDescr.modules[mi];

            cout << "    module" << mi << ": name=" << module.name << ", type=" << module.type << endl;
        }
    }
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

            if (md.data.size)
            {
                cout << "  moduleIndex=" << mi << ", data.size=" << md.data.size << endl;
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
