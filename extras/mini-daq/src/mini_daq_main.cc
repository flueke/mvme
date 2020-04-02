#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include <mesytec_mvlc.h>
#include <util/storage_sizes.h>
#include <mvlc_impl_usb.h>
#include <util/string_view.hpp>

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;
using namespace nonstd;

int main(int argc, char *argv[])
{
    auto mvlc = make_mvlc_usb();

    if (auto ec = mvlc.connect())
    {
        cerr << "Error connecting to MVLC: " << ec.message() << endl;
        return 1;
    }

    if (auto ec = disable_all_triggers(mvlc))
    {
        cerr << "Error disabling MVLC triggers: " << ec.message() << endl;
        return 1;
    }

    struct Write
    {
        u32 address;
        u16 value;
    };

    auto do_write = [&mvlc] (u32 address, u16 value)
    {
        if (auto ec = mvlc.vmeWrite(address, value, vme_amods::A32, VMEDataWidth::D16))
            throw ec;
    };

    // Module init: assuming an MTDC at adress 0x00000000
    u32 base = 0x00000000u;
    u8 mcstByte = 0xbbu;
    u32 mcst = mcstByte << 24;
    u8 stackId = 1;
    u8 irq = 1;

    try
    {
        // module reset
        do_write(base + 0x6008, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::vector<Write> writes =
        {
            { base + 0x6070, 7 }, // pulser
            { base + 0x6010, irq }, // irq
            { base + 0x601c, 0 },
            { base + 0x601e, 1 }, // irq fifo threshold in events
            { base + 0x6038, 0 }, // eoe marker
            { base + 0x6036, 0xb }, // multievent mode
            { base + 0x601a, 1 }, // max transfer data
            { base + 0x6020, 0x80 }, // enable mcst
            { base + 0x6024, mcstByte }, // mcst address
        };

        for (const auto &write: writes)
            do_write(write.address, write.value);
    }
    catch (const std::error_code &ec)
    {
        cerr << "Error initializing module: " << ec.message() << endl;
        return 1;
    }

    // mcst daq start sequence
    try
    {
        std::vector<Write> writes =
        {
            { mcst + 0x603a, 0 },
            { mcst + 0x6090, 3 },
            { mcst + 0x603c, 1 },
            { mcst + 0x603a, 1 },
            { mcst + 0x6034, 1 },
        };

        for (const auto &write: writes)
            do_write(write.address, write.value);
    }
    catch (const std::error_code &ec)
    {
        cerr << "Error running MCST DAQ start sequence: " << ec.message() << endl;
        return 1;
    }

    // readout stack building and upload
    StackCommandBuilder readoutStack;
    readoutStack.beginGroup("readout");
    readoutStack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max());
    readoutStack.beginGroup("reset");
    readoutStack.addVMEWrite(mcst + 0x6034, 1, vme_amods::A32, VMEDataWidth::D16);

    if (auto ec = reset_stack_offsets(mvlc))
    {
        cerr << "Error writing stack offset registers: " << ec.message() << endl;
        return 1;
    }

    if (auto ec = setup_readout_stacks(mvlc, { readoutStack }))
    {
        cerr << "Error uploading readout stack: " << ec.message() << endl;
        return 1;
    }

    if (auto ec = setup_stack_trigger(mvlc, stackId, stacks::TriggerType::IRQNoIACK, irq))
    {
        cerr << "Error setting up stack trigger: " << ec.message() << endl;
        return 1;
    }

    // readout loop
    struct Buffer
    {
        std::vector<u8> buffer;
        size_t used = 0u;
    };

    size_t totalBytesTransferred = 0u;
    auto timeToRun = std::chrono::seconds(10);
    auto tStart = std::chrono::steady_clock::now();
    Buffer readBuffer, tempBuffer;
    readBuffer.buffer.resize(Megabytes(1));
    auto mvlcUSB = reinterpret_cast<usb::Impl *>(mvlc.getImpl());

    while (true)
    {
        auto elapsed = std::chrono::steady_clock::now() - tStart;

        if (elapsed >= timeToRun)
            break;

        size_t bytesTransferred = 0u;
        auto dataGuard = mvlc.getLocks().lockData();
        auto ec = mvlcUSB->read_unbuffered(
            Pipe::Data, readBuffer.buffer.data(), readBuffer.buffer.size(), bytesTransferred);
        dataGuard.unlock();
        readBuffer.used = bytesTransferred;
        totalBytesTransferred += bytesTransferred;

        if (ec == ErrorType::ConnectionError)
        {
            cerr << "Lost connection to MVLC, leaving readout loop: " << ec.message() << endl;
            break;
        }

        basic_string_view<u32> bufferView(
            reinterpret_cast<const u32 *>(readBuffer.buffer.data()),
            readBuffer.used / sizeof(u32));

        for (const auto &value: bufferView)
            printf("0x%08x\n", value);
        //if (readBuffer.used >= sizeof(u32))
        //{
        //    printf("0x%08x\n", *reinterpret_cast<const u32 *>(readBuffer.buffer.data()));
        //}

        // TODO: follow usb buffer framing, store leftover data in tempBuffer,
        // pass buffers to consumers, track buffer numbers
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto runDuration = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);
    double runSeconds = runDuration.count() / 1000.0;
    double megaBytes = totalBytesTransferred * 1.0 / Megabytes(1);
    double mbs = megaBytes / runSeconds;

    cout << "totalBytesTransferred=" << totalBytesTransferred << endl;
    cout << "duration=" << runDuration.count() << endl;
    cout << "Ran for " << runSeconds << " seconds, transferred a total of " << megaBytes
        << " MB, resulting data rate: " << mbs << "MB/s"
        << endl;

    if (auto ec = disable_all_triggers(mvlc))
    {
        cerr << "Error disabling MVLC triggers: " << ec.message() << endl;
        return 1;
    }

    mvlc.disconnect();

    return 0;
}
