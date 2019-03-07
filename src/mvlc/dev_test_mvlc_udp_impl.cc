#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_udp.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    auto mvlc = make_mvlc_udp("localhost");

    try
    {
        if (auto ec = mvlc->connect())
            throw ec;

        assert(mvlc->isConnected());

        std::string msg = "Hello, World!\n";
        std::vector<char> data;
        std::copy(msg.begin(), msg.end(), std::back_inserter(data));
        size_t bytesTransferred = 0u;

        if (auto ec = mvlc->write(Pipe::Command,
                                  reinterpret_cast<const u8 *>(data.data()), data.size(),
                                  bytesTransferred))
        {
            throw ec;
        }

    } catch (const std::error_code &ec)
    {
        cerr << "caught an error_code: " << ec.category().name() << ": " << ec.message()
            << " (" << ec.value() << ")" << endl;
        return 1;
    }

    cout << "Hit enter to exit";
    getchar();

    return 0;
}
