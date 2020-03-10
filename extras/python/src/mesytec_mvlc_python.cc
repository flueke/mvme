#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "mvlc_commands.h"

namespace py = pybind11;

using namespace mesytec::mvlc;

PYBIND11_MODULE(mesytec_mvlc_python, m)
{
    m.doc() = "driver library for the Mesytec MVLC VME controller";

    py::enum_<VMEDataWidth>(m, "VMEDataWidth")
        .value("D16", VMEDataWidth::D16)
        .value("D32", VMEDataWidth::D32)
        ;

    py::enum_<StackCommandType>(m, "StackCommandType")
        .value("StackStart", StackCommandType::StackStart)
        .value("StackEnd", StackCommandType::StackEnd)
        .value("VMEWrite", StackCommandType::VMEWrite)
        .value("VMERead", StackCommandType::VMERead)
        .value("WriteMarker", StackCommandType::WriteMarker)
        .value("WriteSpecial", StackCommandType::WriteSpecial)
        ;

    py::enum_<SuperCommandType>(m, "SuperCommandType")
        .value("CmdBufferStart", SuperCommandType::CmdBufferStart)
        .value("CmdBufferEnd", SuperCommandType::CmdBufferEnd)
        .value("ReferenceWord", SuperCommandType::ReferenceWord)
        .value("ReadLocal", SuperCommandType::ReadLocal)
        .value("ReadLocalBlock", SuperCommandType::ReadLocalBlock)
        .value("WriteLocal", SuperCommandType::WriteLocal)
        .value("WriteReset", SuperCommandType::WriteReset)
        ;

    py::class_<SuperCommand>(m, "SuperCommand")
        .def(py::init<>())
        .def_readwrite("type", &SuperCommand::type)
        .def_readwrite("address", &SuperCommand::address)
        .def_readwrite("value", &SuperCommand::value)
        ;

    py::class_<SuperCommandBuilder>(m, "SuperCommandBuilder")
        .def(py::init<>())
        .def("addReferenceWord", &SuperCommandBuilder::addReferenceWord)
        .def("addReadLocal", &SuperCommandBuilder::addReadLocal)
        .def("addReadLocalBlock", &SuperCommandBuilder::addReadLocalBlock)
        .def("addWriteLocal", &SuperCommandBuilder::addWriteLocal)
        .def("addWriteReset", &SuperCommandBuilder::addWriteReset)
        .def("addCommands", &SuperCommandBuilder::addCommands)
        .def("addVMERead", &SuperCommandBuilder::addVMERead)
        .def("addVMEWrite", &SuperCommandBuilder::addVMEWrite)
        .def("addVMEBlockRead", &SuperCommandBuilder::addVMEBlockRead)
        .def("getCommands", &SuperCommandBuilder::getCommands)
        ;
}
