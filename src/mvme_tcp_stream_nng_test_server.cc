#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/protected.h>
#include <mesytec-mvlc/util/signal_handling.h>
#include <mesytec-mvlc/util/stopwatch.h>
#include <nng_stream_server.h>
#include <util/mesy_nng.h>

using namespace mesytec;
using namespace mesytec::mvlc;

static const std::vector<std::string> listenUris = {
    "tcp4://*:42333",
    "ipc:///tmp/mvme_tcp_stream_server.ipc",
    "tcp4://*:42334",
};

int main()
{
    {
        mvlc::set_global_log_level(spdlog::level::debug);
        mvlc::util::setup_signal_handlers();

        nng::NngStreamServer ctx;
        ctx.start(listenUris);

        // Main loop - send data periodically to all clients
        u32 bufferNumber = 0;
        std::vector<u32> data = {1, 2, 3, 4, 5};

        while (!mvlc::util::signal_received())
        {
            // spdlog::info("Main loop iteration, {} clients connected. bufferNumber={}",
            // ctx.clients.size(), bufferNumber);

            if (!send_to_all_clients(&ctx, bufferNumber, data.data(), data.size()))
            {
                spdlog::warn("Failed to send data to all clients");
            }

            bufferNumber++;
            std::for_each(std::begin(data), std::end(data), [](u32 &val) { val++; });
        }
    }

    spdlog::info("left main scope. ServerContext should be destroyed by now");
    nng_fini(); // Note: don't do this in mvme. It destroys state that NNG needs to operate. Helps
                // valgrind though.
    return 0;
}
