//#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
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

struct Context
{
    py::scoped_interpreter interp;
    py::module code;
    py::object py_begin_run;
    py::object py_event_data;
    py::object py_end_run;
};

PYBIND11_EMBEDDED_MODULE(py_listfile_reader, m)
{
    py::class_<ModuleReadoutDescription>(m, "ModuleReadoutDescription")
        .def_readonly("name", &ModuleReadoutDescription::name)
        .def_readonly("type", &ModuleReadoutDescription::type)
        .def_readonly("prefixLen", &ModuleReadoutDescription::prefixLen)
        .def_readonly("suffixLen", &ModuleReadoutDescription::suffixLen)
        .def_readonly("hasDynamic", &ModuleReadoutDescription::hasDynamic)
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
}

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
    ctx->code = py::module::import("python_printer");

    // TODO: check if the retrieved attributes are callable
    ctx->py_begin_run = ctx->code.attr("begin_run");
    ctx->py_event_data = ctx->code.attr("event_data");
    ctx->py_end_run = ctx->code.attr("end_run");

    return ctx;
}

void plugin_destroy (Context *ctx)
{
    delete ctx;
}

void begin_run (Context *ctx, const RunDescription *run)
{
    cout << __PRETTY_FUNCTION__ << endl;
    
    ctx->code.reload();

    DataBlock test { nullptr, 3 };

    ctx->py_begin_run(test);
}

void event_data (Context *ctx, int eventIndex, const ModuleData *modules, int moduleCount)
{
    ctx->py_event_data(eventIndex, moduleCount);
}

void end_run (Context *ctx)
{
    cout << __PRETTY_FUNCTION__ << endl;
    ctx->py_end_run("pypypy end_run");
}

}
