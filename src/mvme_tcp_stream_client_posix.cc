#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// host lookup
std::optional<sockaddr_in> lookup(const std::string &host, std::uint16_t port)
{
    if (host.empty())
        return {};

    sockaddr_in ret = {};

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = nullptr, *rp = nullptr;

    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);

    if (rc != 0)
    {
        std::cerr << "Failed to resolve host " << host << ": " << gai_strerror(rc) << "\n";
        return {};
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        if (rp->ai_addrlen == sizeof(ret))
        {
            std::memcpy(&ret, rp->ai_addr, rp->ai_addrlen);
            break;
        }
    }

    freeaddrinfo(result);

    if (!rp) // did not find a suitable address
        return {};

    return ret;
}

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

        auto addr = lookup(host, static_cast<std::uint16_t>(std::stoi(port)));

        if (!addr)
        {
            return 1;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0)
        {
            std::cerr << "Failed to create socket: " << strerror(errno) << "\n";
            return 1;
        }

        std::cout << "Connecting to " << host << ":" << port << "...\n";

        if (connect(sock, reinterpret_cast<const sockaddr *>(&addr.value()), sizeof(addr.value())) <
            0)
        {
            std::cerr << "Failed to connect: " << strerror(errno) << "\n";
            close(sock);
            return 1;
        }

        std::cout << "Connected successfully!\n";

        std::uint32_t bufferNumber = 0;
        std::uint32_t bufferSize = 0;
        std::vector<std::uint32_t> destBuffer;

        while (true)
        {
            ssize_t bytesRead = recv(sock, &bufferNumber, sizeof(bufferNumber), MSG_WAITALL);
            if (bytesRead == 0)
                break; // Connection closed
            if (bytesRead < 0)
            {
                std::cerr << "Error reading buffer number: " << strerror(errno) << "\n";
                break;
            }

            bytesRead = recv(sock, &bufferSize, sizeof(bufferSize), MSG_WAITALL);
            if (bytesRead == 0)
                break; // Connection closed
            if (bytesRead < 0)
            {
                std::cerr << "Error reading buffer size: " << strerror(errno) << "\n";
                break;
            }

            destBuffer.resize(bufferSize);

            bytesRead = recv(sock, destBuffer.data(), destBuffer.size() * sizeof(std::uint32_t),
                             MSG_WAITALL);
            if (bytesRead == 0)
                break; // Connection closed
            if (bytesRead < 0)
            {
                std::cerr << "Error reading buffer data: " << strerror(errno) << "\n";
                break;
            }

            destBuffer.resize(bytesRead / sizeof(std::uint32_t));

            auto bufferView = std::basic_string_view<std::uint32_t>(
                reinterpret_cast<const std::uint32_t *>(destBuffer.data()),
                std::min(destBuffer.size(), static_cast<std::size_t>(10)));

            std::cout << "Received buffer " << bufferNumber << " of size " << bufferSize << ": ";

            for (const auto &val: bufferView)
                std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << val << " ";

            std::cout << std::dec << "\n";
        }

        close(sock);

        return 0;
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
