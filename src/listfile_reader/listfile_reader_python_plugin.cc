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
//#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <array>
#include <iostream>
#include <dlfcn.h>

#include "listfile_reader.h"

using std::cout;
using std::endl;

namespace py = pybind11;

static const char *PluginName = "Python Plugin";
static const char *PluginDescription = "Passes event data to a python script";

namespace
{

PYBIND11_EMBEDDED_MODULE(py_listfile_reader, m)
{
    py::class_<ModuleReadoutDescription>(m, "ModuleReadoutDescription")
        .def_readonly("name", &ModuleReadoutDescription::name)
        .def_readonly("type", &ModuleReadoutDescription::type)
        .def_readonly("prefixLen", &ModuleReadoutDescription::prefixLen)
        .def_readonly("suffixLen", &ModuleReadoutDescription::suffixLen)
        .def_readonly("hasDynamic", &ModuleReadoutDescription::hasDynamic)
        ;

    py::class_<EventReadoutDescription>(m, "EventReadoutDescription")
        .def_readonly("name", &EventReadoutDescription::name)
        .def("getModules", [] (const EventReadoutDescription &erd) {
            std::vector<ModuleReadoutDescription> result;
            std::copy(erd.modules, erd.modules + erd.moduleCount,
                      std::back_inserter(result));
            return result;
        })
        ;

    py::class_<RunDescription>(m, "RunDescription")
        .def_readonly("listfileFilename", &RunDescription::listfileFilename)
        .def("getEvents", [] (const RunDescription &rd) {
            std::vector<EventReadoutDescription> result;
            std::copy(rd.events, rd.events + rd.eventCount,
                      std::back_inserter(result));
            return result;
        })
        ;

    py::class_<DataBlock>(m, "DataBlock", py::buffer_protocol())
        .def_buffer([] (DataBlock &block) -> py::buffer_info {
            return py::buffer_info(
                const_cast<uint32_t *>(block.data),
                sizeof(uint32_t),
                py::format_descriptor<uint32_t>::format(),
                1,
                { static_cast<ssize_t>(block.size) },
                { sizeof(uint32_t) }
            );
        });

    py::class_<ModuleData>(m, "ModuleData")
        .def_readonly("prefix", &ModuleData::prefix)
        .def_readonly("dynamic", &ModuleData::dynamic)
        .def_readonly("suffix", &ModuleData::suffix)
        ;
}

struct Context
{
    py::scoped_interpreter interp;
    py::module usercode;
    py::object userobject;
    py::object py_begin_run;
    py::object py_event_data;
    py::object py_end_run;
    std::vector<ModuleData> moduleDataBuffer;
};

}

extern "C"
{

void plugin_info (const char **plugin_name, const char **plugin_description)
{
    *plugin_name = PluginName;
    *plugin_description = PluginDescription;
}

void *plugin_init (const char *pluginFilename, int argc, const char *argv[])
{
    auto ctx = new Context{};

    // TODO: catch exceptions
    if (argc > 0)
    {
        ctx->usercode = py::module::import(argv[0]);
    }
    else
    {
        ctx->usercode = py::module::import("listfile_reader_python_printer");
    }

    // TODO: check if the retrieved attributes are callable
    ctx->py_begin_run = ctx->usercode.attr("begin_run");
    ctx->py_event_data = ctx->usercode.attr("event_data");
    ctx->py_end_run = ctx->usercode.attr("end_run");

    return ctx;
}

void plugin_destroy (Context *ctx)
{
    delete ctx;
}

void begin_run (Context *ctx, const RunDescription *run)
{
    cout << __PRETTY_FUNCTION__ << endl;

    ctx->usercode.reload();

    // TODO: check if the retrieved attributes are callable
    ctx->py_begin_run = ctx->usercode.attr("begin_run");
    ctx->py_event_data = ctx->usercode.attr("event_data");
    ctx->py_end_run = ctx->usercode.attr("end_run");

    ctx->userobject = ctx->py_begin_run(*run);
}

void event_data (Context *ctx, int eventIndex, const ModuleData *modules, int moduleCount)
{
    ctx->moduleDataBuffer.clear();
    std::copy(modules, modules+moduleCount, std::back_inserter(ctx->moduleDataBuffer));
    ctx->py_event_data(eventIndex, ctx->moduleDataBuffer);
}

void end_run (Context *ctx)
{
    cout << __PRETTY_FUNCTION__ << endl;
    ctx->py_end_run(ctx->userobject);
}

}
