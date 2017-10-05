/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#ifndef __BITS_H__
#define __BITS_H__

/* Note: This code was taken from the FXT library by Joerg Arndt and modified
 * to work with u32 instead of ulong. The original code was in bits/bitgather.h
 *
 * See http://www.jjj.de/fxt/ for details on the FXT library.
 */

#include "../typedefs.h"

static inline u32 bit_gather(u32 w, u32 m)
// Return  word with bits of w collected as indicated by m:
// Example:
//  w = 00A0BC00
//  m = 00101100
//  ==> 00000ABC
// This is the inverse of bit_scatter()
{
    u32 z = 0;
    u32 b = 1;
    while ( m )
    {
        u32 i = m & -m;  // lowest bit
        m ^= i;  // clear lowest bit in m
        z += (i&w ? b : 0);
        b <<= 1;
    }
    return  z;
}
// -------------------------

static inline u32 bit_scatter(u32 w, u32 m)
// Return  word with bits of w distributed as indicated by m:
// Example:
//  w = 00000ABC
//  m = 00101100
//  ==> 00A0BC00
// This is the inverse of bit_gather()
{
    u32 z = 0;
    u32 b = 1;
    while ( m )
    {
        u32 i = m & -m;  // lowest bit
        m ^= i;
        z += (b&w ? i : 0);
        b <<= 1;
    }
    return  z;
}
// -------------------------

#endif /* __BITS_H__ */
