
#include <future>

#include <pybind11/embed.h>
#include <pybind11/iostream.h>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

namespace py = pybind11;
using namespace py::literals;
using namespace std::chrono_literals;


int main(int argc, char *argv[])
{
    py::scoped_interpreter guard{false}; // start the interpreter and keep it alive

    auto f = std::async(std::launch::async, [] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        fmt::print("interrupter thread is {}\n", std::this_thread::get_id());
        PyErr_SetString(PyExc_KeyboardInterrupt, "Foobar");
        PyErr_SetInterrupt();
    });

    fmt::print("py thread is {}\n", std::this_thread::get_id());
    try
    {
        py::exec(R"(
            #print("Hello Sink, sleeping now for 5s!", flush=True)
            #import time
            #time.sleep(5)
            #print("Hello Sink, done sleeping", flush=True)
            def handler(signal_num, stackframe):
                print("Signal Handler invoked!", flush=True)
                #raise Exception("signal handler")
                raise KeyboardInterrupt()
            #import signal
            #signal.signal(signal.SIGINT, handler)
            import threading
            print("py: Entering infinite loop...", flush=True)
            while True: pass
            )");
    }
    catch (const py::error_already_set &e)
    {
        fmt::print("Exception from py::exec(): {}", e.what());
    }

    f.get();
}
