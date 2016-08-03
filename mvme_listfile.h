#ifndef UUID_c25729bd_96ba_4b27_9d14_f53626039101
#define UUID_c25729bd_96ba_4b27_9d14_f53626039101

/*
 * ===== MVME Listfile format =====
 *
 * Section Header:
 *  Type
 *  Size
 *  Type specific info
 *
 * Header Types:
 * Config
 * Event
 *
 */

/*
 *  ------- Section Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttt         eeeessssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed)


 *  ------- Subevent Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |              mmmmmm  ssssssssss|
 * +--------------------------------+
*/

#include <QTextStream>
#include "databuffer.h"
#include "util.h"

namespace listfile
{
    enum SectionType
    {
        // The config section contains the mvmecfg as a json string padded with
        // zeros to the next 32 bit boundary. TODO: how to handle configs that
        // are larger than the max section size?
        SectionType_Config = 0,
        SectionType_Event  = 1,
        SectionType_End    = 2,

        SectionType_Max    = 7
    };

    
    static const int SectionTypeMask  = 0xe0000000; // 3 bit section type
    static const int SectionTypeShift = 29;
    static const int SectionSizeMask  = 0xffff;    // 16 bit section size in 32 bit words
    static const int SectionSizeShift = 0;
    static const int EventTypeMask  = 0xf0000;   // 4 bit event type
    static const int EventTypeShift = 16;

    // Subevent containing module data
    static const int ModuleTypeMask  = 0x3f000; // 6 bit module type
    static const int ModuleTypeShift = 12;

    static const int SubEventSizeMask  = 0x3ff; // 10 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;
}

void dump_event_buffer(QTextStream &out, const DataBuffer *eventBuffer);

#endif
