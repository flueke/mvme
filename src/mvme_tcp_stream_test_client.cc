#if 1
#include <asio.hpp>
#include <cassert>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/signal_handling.h>

using asio::ip::tcp;
using namespace mesytec;

int main(int argc, char *argv[])
{
    std::string host = "127.0.0.1";
    std::string port = "42333";

    if (argc > 1)
        host = argv[1];

    if (argc > 2)
        port = argv[2];

    try
    {
        mvlc::util::setup_signal_handlers();
        asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(host, port);
        tcp::socket socket(io_context);
        asio::connect(socket, endpoints);
        std::vector<std::uint32_t> destBuffer;
        size_t bufferNumber = 0;

        while (!mvlc::util::signal_received())
        {
            std::uint32_t bufferNumber = 0u;
            std::uint32_t bufferSize = 0u;

            auto a = asio::mutable_buffer(&bufferNumber, sizeof(bufferNumber));
            auto b = asio::mutable_buffer(&bufferSize, sizeof(bufferSize));

            auto bytesRead = asio::read(socket, std::array<asio::mutable_buffer, 2>{a, b});
            assert(bytesRead == sizeof(bufferNumber) + sizeof(bufferSize));

            destBuffer.resize(bufferSize);
            bytesRead = asio::read(socket, asio::buffer(destBuffer.data(), destBuffer.size() * sizeof(std::uint32_t)));
            assert(bytesRead == destBuffer.size() * sizeof(std::uint32_t));
            auto bufferView = std::basic_string_view<std::uint32_t>(reinterpret_cast<const std::uint32_t *>(
              destBuffer.data()), std::min(bufferSize, static_cast<std::uint32_t>(10)));
            fmt::println("Received buffer {} of size {}: {:#010x}...", bufferNumber, bufferSize, fmt::join(bufferView, ", "));
        }
    }
    catch(const std::exception& e)
    {
        fmt::println("Error: {}", e.what());
    }
}

#endif

#if 0
//
// client.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2024 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <array>
#include <iostream>
#include <asio.hpp>
#include <mesytec-mvlc/util/fmt.h>

using asio::ip::tcp;

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: client <host>" << std::endl;
      return 1;
    }

    asio::io_context io_context;

    tcp::resolver resolver(io_context);
    tcp::resolver::results_type endpoints =
      resolver.resolve(argv[1], "42333");

    tcp::socket socket(io_context);
    asio::connect(socket, endpoints);

    for (;;)
    {
      std::array<char, 128> buf;
      std::error_code error;

      size_t len = socket.read_some(asio::buffer(buf), error);

      if (error == asio::error::eof)
        break; // Connection closed cleanly by peer.
      else if (error)
        throw std::system_error(error); // Some other error.

      //std::cout.write(buf.data(), len);
      fmt::println("Received: {} bytes", buf.size());
    }
  }
  catch (std::exception& e)
  {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
#endif
