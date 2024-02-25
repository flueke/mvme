#include <future>
#include <ostream>
#include <sstream>
#include <iostream>
#include <chrono>

#include <pybind11/embed.h>
#include <pybind11/iostream.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <QByteArray>
#include <QCoreApplication> // QCoreApplication::applicationDirPath()
#include <QDebug>
#include <QFileInfo>
#include <QtGlobal>

namespace py = pybind11;
using namespace py::literals;
using namespace std::chrono_literals;

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

using StringOutputFunc = std::function<void (const std::string &str)>;

class BufferedOutputSink
{
    public:
        BufferedOutputSink(StringOutputFunc sink)
            : sink_(sink)
        {}

        ~BufferedOutputSink()
        {
            // flush(); // TODO: could flush on destruction. Currently it's done
            // below in PyStdErrOutStreamSinkRedirect.
            qDebug() << __PRETTY_FUNCTION__;
        }

        py::size_t write(py::object py_buffer)
        {
            buffer_ << py_buffer.cast<std::string>();
            auto result = py::len(py_buffer);
            return result;
        }

        void flush()
        {
            std::cerr << "[flush!]";
            if (sink_ && !buffer_.str().empty())
            {
                sink_(buffer_.str());
                buffer_.str({}); // clear the internal string buffer.
            }
        }

    private:
        StringOutputFunc sink_;
        std::stringstream buffer_;
};

class PyStdErrOutStreamSinkRedirect
{
    public:
        PyStdErrOutStreamSinkRedirect(StringOutputFunc stdout_sink, StringOutputFunc stderr_sink = {})
        {
            // Store previous sys.stdout/stderr objects.
            auto sysm = py::module::import("sys");
            prev_stdout_ = sysm.attr("stdout");
            prev_stderr_ = sysm.attr("stderr");

            // Need to import the output_redirect embedded module for
            // BufferedOutputSink to be available in python.
            py::module_::import("output_redirect");

            // Replace sys.stdout/stderr with BufferedOutputSink instances
            sysm.attr("stdout") = std::make_unique<BufferedOutputSink>(stdout_sink);
            sysm.attr("stderr") = std::make_unique<BufferedOutputSink>(stderr_sink ? stderr_sink : stdout_sink);
        }

        ~PyStdErrOutStreamSinkRedirect()
        {
            qDebug() << __PRETTY_FUNCTION__ << "begin";
            auto sysm = py::module::import("sys");

            // Force flush to the output sinks.
            sysm.attr("stdout").attr("flush")();
            sysm.attr("stderr").attr("flush")();

            // Restore previous sys.stdout/stderr objects.
            sysm.attr("stdout") = prev_stdout_;
            sysm.attr("stderr") = prev_stderr_;
            qDebug() << __PRETTY_FUNCTION__ << "end";
        }

    private:
        py::object prev_stdout_;
        py::object prev_stderr_;
};

PYBIND11_EMBEDDED_MODULE(output_redirect, m)
{
    py::class_<BufferedOutputSink>(m, "BufferedOutputSink")
        // Note: no constructor defintion here -> cannot be created from within python.
        .def("write", &BufferedOutputSink::write)
        .def("flush", &BufferedOutputSink::flush)
        ;
}

void put_env(const char *varName, const QByteArray &value)
{
    if (!qputenv(varName, value))
        throw std::runtime_error(fmt::format("Could not set env variable {}", varName));
    qDebug() << "put_env" << varName << "=" << value;
}

// TODO: test
int main(int argc, char *argv[])
{
    //QCoreApplication app(argc, argv);

#ifdef __WIN32
    // These variables are needed for the embedded python to work when started from a clean env.
    // When distributing python with mvme these must be set to the mvme installation directory.
    //put_env("PYTHONHOME", R"(C:\msys64\mingw64\bin)");
    //put_env("PYTHONPATH", R"(C:\msys64\mingw64\lib\python3.10)");

    {
        // Windows deployment contains the embeddable python under 'bin/python'.
        auto localPyPath = QCoreApplication::applicationDirPath() + "/python";
        qDebug() << "checking for local py path:" << localPyPath;
        QFileInfo pyPathInfo(localPyPath);

        if (pyPathInfo.isDir())
        {
            put_env("PYTHONHOME", pyPathInfo.absolutePath().toLocal8Bit());
        }
    }

#endif

    py::scoped_interpreter guard{false}; // start the interpreter and keep it alive

    #if 0
    std::cerr << ">>>>>>>>>> before first output redirection ====================" << std::endl;

    {
        PyStdErrOutStreamRedirect pyOutputRedirect;

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
    #endif

    std::cerr << "========== after first output redirection ====================" << std::endl;


    #if 1
    {
        auto stdout_sink = [](const std::string &str)
        {
            fmt::print("stdout_sink thread is {}\n", std::this_thread::get_id());
            std::cout << "[stdout_sink]: '" << str << "'" << std::endl;
        };

        auto stderr_sink = [](const std::string &str)
        {
            std::cout << "[stderr_sink]: '" << str << "'" << std::endl;
        };

        //PyStdErrOutStreamSinkRedirect pyOutputRedirect(stdout_sink, stderr_sink);

        {
            fmt::print("main thread is {}\n", std::this_thread::get_id());
            #if 1
            py::exec(R"(
                print("py: Setting up signal handler for SIGINT", flush=True)
                import signal
                signal.signal(signal.SIGINT, signal.SIG_DFL)
            )");
            #endif
            py::gil_scoped_release gil_release;
            auto f = std::async(std::launch::async, [] {
                py::gil_scoped_acquire gil_acquire;
                fmt::print("py thread is {}\n", std::this_thread::get_id());
                py::exec(R"(
                    #print("Hello Sink, sleeping now for 5s!", flush=True)
                    #import time
                    #time.sleep(5)
                    #print("Hello Sink, done sleeping", flush=True)
                    print("py: Entering infinite loop...", flush=True)
                    while True: pass
                    )");
            });

            std::future_status status;

            do
            {
                switch (status = f.wait_for(1s); status)
                {
                case std::future_status::deferred:
                    std::cout << "deferred\n";
                    break;
                case std::future_status::timeout:
                    std::cout << "timeout\n";
                    PyErr_SetString(PyExc_KeyboardInterrupt, "...");
                    // PyErr_SetInterrupt();
                    break;
                case std::future_status::ready:
                    std::cout << "ready!\n";
                    break;
                }
            } while (status != std::future_status::ready);
            //auto f_status = f.wait_for(std::chrono::milliseconds(100));
            //while (f.wait_for(std::chrono::milliseconds(100) != std::future_status::ready))
            //{
            //    fmt::print("PyErr_SetInterrupt!\n");
            //    PyErr_SetInterrupt();
            //}
            //std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            //PyErr_SetInterrupt();
            //PyErr_SetString(PyExc_KeyboardInterrupt, "...");
            f.get();
        }


    }
    #else
    {
        auto stdout_sink_func = [](const std::string &str)
        {
            std::cout << "[stdout_sink]: '" << str << "'" << std::endl;
        };

        auto redir_module = py::module_::import("output_redirect"); // So that python knows about BufferedOutpuSink.
        auto stdout_sink = std::make_unique<BufferedOutputSink>(stdout_sink_func);
        py::module_::import("sys").attr("stdout") = std::move(stdout_sink);
        py::exec(R"(
            print("Hello Sink!")
            )");
        // Force flush the BufferedOutputSink here
        py::module_::import("sys").attr("stdout").attr("flush")();
    }
    #endif

    std::cerr  << "<<<<<<<<<< after second output redirection ====================" << std::endl;
}
