#include "mvlc/mvlc_usb_impl.h"
#include <cassert>
#include <iostream>

using namespace mesytec::mvlc::usb;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    Impl mvlc(0);

    auto ec = mvlc.connect();

    if (ec)
    {
        assert(!mvlc.is_connected());
        cerr << ec.category().name() << ": " << ec.message() << endl;
        return 1;
    }

    assert(mvlc.is_connected());

    ec = mvlc.disconnect();
    assert(!mvlc.is_connected());
}
