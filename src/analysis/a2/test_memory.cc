/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#if 0
#include <cstdio>
#include <iostream>
using std::cout;
using std::endl;

using namespace memory;

int main(int argc, char *argv[])
{
    cout << "alignments" << endl;
    cout << "u32 " << alignof(u32) << endl;
    cout << "u8  " << alignof(u8) << endl;
    cout << "f64 " << alignof(double) << endl;


    const size_t size = Kilobytes(1);

    Arena arena(size);

    assert(arena.mem);
    assert(arena.size == size);
    assert(arena.free() == size);

    auto *ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);
    assert(arena.free() == size - sizeof(u32));

    auto *cp = arena.pushStruct<u8>();
    printf("cp=%p\n", cp);
    assert(cp);

    cp = arena.pushStruct<u8>();
    printf("cp=%p\n", cp);
    assert(cp);

    ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);

    ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);

    ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);

#if 1
    ip = arena.pushArray<u32>(2);
    printf("ip=%p\n", ip);
    assert(ip);
#else
    ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);
#endif
    ip = arena.pushStruct<u32>();
    printf("ip=%p\n", ip);
    assert(ip);

    u64 iterations = 0;

    while (true)
    {
        auto ip = arena.pushStruct<u32>();
        if (!ip) break;
        ++iterations;
    }

    cout << "iterations: " << iterations << endl;

    // arena clear ===================
    arena.reset();
    assert(arena.free() == size);

    struct Foo
    {
        double x = 3.14;
        u32 i = 42;
    };

    Foo f;

    auto foo_p = arena.push(f);
    assert(foo_p);
    assert(foo_p->x == 3.14);
    assert(foo_p->i == 42);

    foo_p = arena.push<Foo>({1.0, 36});
    assert(foo_p);
    assert(foo_p->x == 1.0);
    assert(foo_p->i == 36);
}
#endif

