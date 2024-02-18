#include <pybind11/embed.h>
#include <pybind11/iostream.h>
#include <ostream>
#include <sstream>

#include <iostream>

namespace py = pybind11;
using namespace py::literals;

#if 0
class my_buffer: public std::stringbuf
{
    public:
        explicit my_buffer(const std::string &prefix)
            : prefix_(prefix)
        {}

        int sync() override
        {
            std::cout << "my_buffer::sync()[" << prefix_ << "]: " << str();
            str().clear();
            return 0;
        }

    private:
        std::string prefix_;
};

    // Wrong way: redirects the stdout_ostream to write to python! The
    // py::scoped_ostream_redirect constructor replaces the ostreams rdbuf by a
    // custom class that writes _to_ the python stdout.
    //my_buffer stdout_buffer("stdout");
    //std::ostream stdout_ostream(&stdout_buffer);
    //stdout_ostream << "fuuuu" << std::endl; // This goes through python
    //py::scoped_ostream_redirect stdout_redirect(stdout_ostream, py::module::import("sys").attr("stdout"));
#endif

// From https://github.com/pybind/pybind11/issues/1622#issuecomment-452718093
class PyStdErrOutStreamRedirect {
    py::object _stdout;
    py::object _stderr;
    py::object _stdout_buffer;
    py::object _stderr_buffer;
public:
    PyStdErrOutStreamRedirect() {
        auto sysm = py::module::import("sys");
        _stdout = sysm.attr("stdout");
        _stderr = sysm.attr("stderr");
        auto stringio = py::module::import("io").attr("StringIO");
        _stdout_buffer = stringio();  // Other filelike object can be used here as well, such as objects created by pybind11
        _stderr_buffer = stringio();
        sysm.attr("stdout") = _stdout_buffer;
        sysm.attr("stderr") = _stderr_buffer;
    }
    std::string stdoutString() {
        _stdout_buffer.attr("seek")(0);
        return py::str(_stdout_buffer.attr("read")());
    }
    std::string stderrString() {
        _stderr_buffer.attr("seek")(0);
        return py::str(_stderr_buffer.attr("read")());
    }
    ~PyStdErrOutStreamRedirect() {
        auto sysm = py::module::import("sys");
        sysm.attr("stdout") = _stdout;
        sysm.attr("stderr") = _stderr;
    }
};

int main()
{
    py::scoped_interpreter guard{false}; // start the interpreter and keep it alive

    {
        PyStdErrOutStreamRedirect pyOutputRedirect;
        auto py_stdout = py::module::import("sys").attr("stdout");

        //py::print(py_stdout);
        py::print("Hello, World!"); // use the Python API
        py::exec(R"(print(sys.path))");

        auto globals = py::globals();
        auto locals = py::dict("vme_base"_a = 0xffff0000u);

        py::exec(R"(
        print("{:08x}".format(vme_base))
        my_var = 42
        global my_global
        my_global = 23
        print(f"__name__={__name__}")
        print(locals())
        )",
                 globals, locals);

        py::print("globals seen by c++", py::globals());

        std::cout << "stdoutString: " << pyOutputRedirect.stdoutString();
    }

    py::print("after output redirection");
}
