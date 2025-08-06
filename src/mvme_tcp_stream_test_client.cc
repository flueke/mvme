#include <asio.hpp>
#include <cassert>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/signal_handling.h>

using asio::ip::tcp;
using namespace mesytec;

int main(int argc, char *argv[])
{
    try
    {
        mvlc::util::setup_signal_handlers();
        asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(argv[1], "42333");
        tcp::socket socket(io_context);
        asio::connect(socket, endpoints);
        std::vector<std::uint32_t> destBuffer;
        size_t bufferNumber = 0;

        while (!mvlc::util::signal_received())
        {
            std::uint32_t bufferSize = 0u;
            auto bytesRead = asio::read(socket, asio::buffer(&bufferSize, sizeof(bufferSize)));
            assert(bytesRead == sizeof(bufferSize));
            destBuffer.resize(bufferSize);
            bytesRead = asio::read(socket, asio::buffer(destBuffer.data(), destBuffer.size() * sizeof(std::uint32_t)));
            assert(bytesRead == destBuffer.size() * sizeof(std::uint32_t));
            fmt::println("Received buffer {} of size {}: ", bufferNumber, bufferSize);
        }
    }
    catch(const std::exception& e)
    {
        fmt::println("Error: {}", e.what());
    }
}
