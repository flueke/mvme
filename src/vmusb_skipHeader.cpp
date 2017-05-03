/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2009.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
             NSCL
             Michigan State University
             East Lansing, MI 48824-1321
*/
#include "vmusb_skipHeader.h"
#include <stdint.h>
#include <arpa/inet.h>

static const uint32_t sync1(0xffffffff);
static const uint32_t sync2(0xaa995566);

/**
 * return true if p points to the file's synch header.
 */
static bool checkSyncHeader(uint32_t* p)
{
   return ((p[0] == ntohl(sync1)) && (p[1] == ntohl(sync2)));
}

/**
 * skipHeader
 *   Skips the header of a Xilinx load image
 *
 *   @param pLoadImage - Pointer to storage containing a valid load image.
 *
 *  @return void*       - Pointer to the start of the actual image.
 *  @retval 0           - a NULL pointer is returned if the consistency
 *                        check on the data following the headers fails
 *                        (see notes below).
 *
 * @note - The header data are in network byte ordering.
 *
 * @note - The header consists of three funky headers followed by
 *         several reasonably well formatted fields:
 *         * Header 1 is begins with a uint16_t length. Followed by that many
 *           header bytes.
 *         * Header 2 has the same form as Header1
 *         * Header 3 has the same format as header 1.
 *         * Following the three headers, records that consist of a single
 *           printable Ascii key byte followed by uint16_t size followed by that
 *           many bytes of data.
 *         * For consistency, the first two uint32_t's of the actual data are:
 *           0xffffffff and 0xaa995566. If the first 'unprintable' key byte
 *           position does not lead to this pattern, a null is returned indicating
 *           the file is very likely not a .BIT file.
 */
void*
skipHeader(void* pLoadImage)
{
    uint32_t* pSync;

    /*
       If this is a .bin file the header synch header starts right away:
    */
    pSync = reinterpret_cast<uint32_t*>(pLoadImage);
    if (checkSyncHeader(pSync)) return pLoadImage;

    /*
      The headers are best dealt with both as uint16 and uint8's depending
      on what we're processing
    */
    uint16_t* p16 = reinterpret_cast<uint16_t*>(pLoadImage);
    uint8_t*  p8;

    // skip first funky header:

    uint16_t length = ntohs(*p16++);    // Bytes in funky header 1.
    p8 = reinterpret_cast<uint8_t*>(p16);
    p8 += length;
    p16 = reinterpret_cast<uint16_t*>(p8);

    // p8, p16 are now pointing to the second funky header.

    length = ntohs(*p16++);
    p8 = reinterpret_cast<uint8_t*>(p16);
    p8 += length;
    p16 = reinterpret_cast<uint16_t*>(p8);

    // p8,p16 are pointing to the third funky header:


    length = ntohs(*p16++);
    p8 = reinterpret_cast<uint8_t*>(p16);
    p8 += length;
    p16 = reinterpret_cast<uint16_t*>(p8);

    // Now we're pointing at the first of the ascci keys which is always
    // 'b'.  The Ascii keys are consecutive:
    // Skip the key/counted field pairs:
    char expected = 'b';
    while(*p8 == expected) {
        p8++;
        p16 = reinterpret_cast<uint16_t*>(p8);
        length = ntohs(*p16++);
        p8 = reinterpret_cast<uint8_t*>(p16);
        p8 += length;
        expected++;
    }

    // See if we're at the sync header:

    pSync = reinterpret_cast<uint32_t*>(p8);
    if (checkSyncHeader(pSync)) {
      return p8;
    }

    // Damned stupid Xilinx sometimes does not get the size of the last field right :-(

    p8--;

    // p8 should be pointing at the 0xffffffff, 0xaa995566 pattern.

    pSync = reinterpret_cast<uint32_t*>(p8);
    if (checkSyncHeader(pSync)) {
        return p8;
    } else {
      // Older files screw up the other way:

      p8 += 2;
      pSync = reinterpret_cast<uint32_t*>(p8);
      if (checkSyncHeader(pSync)) {
	return p8;
      } else {
        return 0;
      }
    }

}
