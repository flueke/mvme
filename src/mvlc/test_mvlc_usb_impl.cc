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

    auto ec = mvlc.open();

    if (ec)
    {
        assert(!mvlc.is_open());
        cerr << ec.category().name() << ": " << ec.message() << endl;
        return 1;
    }

    assert(mvlc.is_open());

    ec = mvlc.close();
    assert(!mvlc.is_open());
}
