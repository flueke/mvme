#include <asio.hpp>
#include <iostream>
#include <iomanip>

int main(int argc, char *argv[])
{
    try
    {
        std::string host = "localhost";
        std::string port = "42333";

        if (argc > 1)
            host = argv[1];

        if (argc > 2)
            port = argv[2];

        std::cout << "Connecting to " << host << ":" << port << "...\n";

        asio::io_context io_context;

        asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);

        asio::ip::tcp::socket socket(io_context);
        asio::connect(socket, endpoints);

        //std::uint32_t bufferNumber = 0u;
        //std::uint32_t bufferSize = 0u;
        std::vector<std::uint32_t> destBuffer;

        while (true)
        {
            //asio::mutable_buffer bufferNumberBuf(&bufferNumber, sizeof(bufferNumber));
            //asio::mutable_buffer bufferSizeBuf(&bufferSize, sizeof(bufferSize));

            size_t bytesRead = 0;

            //size_t bytesRead = asio::read(
            //    socket, std::array<asio::mutable_buffer, 2>{bufferNumberBuf, bufferSizeBuf});

            //if (bytesRead == 0)
            //    break; // Connection closed

            destBuffer.clear();
            destBuffer.resize((1u << 20) / sizeof(std::uint32_t));

            bytesRead = asio::read(
                socket, asio::buffer(destBuffer.data(), destBuffer.size() * sizeof(std::uint32_t)));

            if (bytesRead == 0)
                break; // Connection closed

            destBuffer.resize(bytesRead / sizeof(std::uint32_t));

            auto bufferView = std::basic_string_view<std::uint32_t>(
                reinterpret_cast<const std::uint32_t *>(destBuffer.data()),
                std::min(destBuffer.size(), static_cast<std::size_t>(10)));

            //std::cout << "Received buffer " << bufferNumber << " of size " << bufferSize << ": ";
            std::cout << "Received " << bytesRead << " bytes: ";

            for (const auto &val: bufferView)
                std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << val << " ";

            std::cout << std::dec << "\n";
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
