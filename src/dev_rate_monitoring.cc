#include "rate_monitoring.h"
#include <QDebug>
#include <cassert>

int main(int argc, char *argv[])
{
    static const size_t BufferCapacity = 10;

    memory::Arena arena(BufferCapacity);

    auto block = push_typed_block<char>(&arena, BufferCapacity);

    CircularBuffer<char> buffer(block);

    assert(buffer.size() == 0);
    assert(buffer.empty());
    assert(!buffer.full());
    assert(buffer.capacity() == BufferCapacity);

    return 0;
}
