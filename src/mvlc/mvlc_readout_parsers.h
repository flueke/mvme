#ifndef __MVME_MVLC_READOUT_PARSERS_H__
#define __MVME_MVLC_READOUT_PARSERS_H__

#include "vme_script.h"
#include "mvlc/mvlc_impl_eth.h"
#include "mvlc/mvlc_util.h"
#include "databuffer.h"

namespace mesytec
{
namespace mvlc
{

/* analysis calls:
 * getVMEObjectSettings(eventConfig | moduleConfig)
 *  -> multi event processing enabled for an event? module enabled? module header filter.
 *     Multi-Event processing will move into a second stage after event reassembly (linearization).
 * processTimetick
 * beginEvent(eventIndex)
 *   processModuleData(eventIndex, moduleIndex, data *, size) // needs linear data
 * endEvent
 *
 * to be added: processModulePrefix(ei, mi, data *, size);
 *              processModuleSuffix(ei, mi, data *, size);
 *
 *
 * Purpose: The reaodut_parser system is used to parse a possibly lossfull
 * sequence of MVLC readout buffers, reassemble complete readout events and
 * make the reassembled data available to a consumer.
 *
 * Commands that produce output:
 *   marker         -> one word
 *   single_read    -> one word
 *   block_read     -> dynamic part (0xF5 framed)
 *
 * Restrictions per module:
 * - one fixed part
 * - one dynamic block part
 * - one fixed part
 *
* For each Event and Module in the VMEConfig build a ModuleReadoutParts
 * structure from the modules VME readout script.
 * The readout for each module must consist of three parts:
 * a fixed size prefix, a single block transfer and a fixed size suffix. Each
 * of the parts is optional.
 */

struct ModuleReadoutParts
{
    u8 prefixLen; // length in words of the fixed part prefix
    u8 suffixLen; // length in words of the fixed part suffix
    bool hasDynamic; // true if a dynamic part (block read) is present
};

// vme readout script per event and module
using VMEConfReadoutScripts = std::vector<std::vector<vme_script::VMEScript>>;

// ModuleReadoutParts per event and module
using VMEConfReadoutInfo    = std::vector<std::vector<ModuleReadoutParts>>;

ModuleReadoutParts parse_module_readout_script(const vme_script::VMEScript &readoutScript);
VMEConfReadoutInfo parse_vme_readout_info(const VMEConfReadoutScripts &rdoScripts);

struct Span
{
    u32 offset;
    u32 size;
};

struct ModuleReadoutSpans
{
    Span prefixSpan;
    Span dynamicSpan;
    Span suffixSpan;
};

struct end_of_frame: public std::exception {};

struct ReadoutParserCallbacks
{
    std::function<void (int ei)>
        beginEvent = [] (int) {},
        endEvent   = [] (int) {};

    std::function<void (int ei, int mi, u32 *data, u32 size)>
        modulePrefix  = [] (int, int, u32*, u32) {},
        moduleDynamic = [] (int, int, u32*, u32) {},
        moduleSuffix  = [] (int, int, u32*, u32) {};

    std::function<void (u32 *header, u32 size)>
        systemEvent = [] (u32 *, u32) {};
};

struct ReadoutParserCommon
{
    struct FrameParseState
    {
        FrameParseState(u32 frameHeader = 0)
            : header(frameHeader)
            , wordsLeft(extract_frame_info(frameHeader).len)
        {}

        inline operator bool() const { return wordsLeft; }
        inline FrameInfo info() const { return extract_frame_info(header); }

        inline void consumeWord()
        {
            if (wordsLeft == 0)
                throw end_of_frame();
            --wordsLeft;
        }

        u32 header;
        u16 wordsLeft;
    };

    enum ModuleParseState { Prefix, Dynamic, Suffix };

    // The readout workers start with buffer number 1 so buffer 0 can only
    // occur after wrapping the counter. By using 0 as a starting value the
    // buffer loss calculation will work without special cases.
    u32 lastBufferNumber = 0;

    // Space to assemble linear readout data.
    DataBuffer workBuffer;

    // Current output offset into the workbuffer
    u32 workBufferOffset = 0;

    // Per event preparsed module readout info.
    VMEConfReadoutInfo readoutInfo;

    // Per module offsets and sizes into the workbuffer.
    std::vector<ModuleReadoutSpans> readoutDataSpans;

    int eventIndex = -1;
    int moduleIndex = -1;
    ModuleParseState moduleParseState;
    FrameParseState curStackFrame = {};
    FrameParseState curBlockFrame = {};
};

struct ReadoutParser_ETH: public ReadoutParserCommon
{
    // The actual packet number is of type u16. Using an s32 here to represent
    // the "no previous packet" case by storing a -1.
    s32 lastPacketNumber = -1;
};

struct ReadoutParser_USB: public ReadoutParserCommon
{
};

ReadoutParser_ETH *make_readout_parser_eth(const VMEConfReadoutScripts &readoutScripts);
ReadoutParser_USB *make_readout_parser_usb(const VMEConfReadoutScripts &readoutScripts);

void parse_readout_buffer(
    ReadoutParser_ETH &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize);

void parse_readout_buffer(
    ReadoutParser_USB &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, u8 *buffer, size_t bufferSize);

} // end namespace mesytec
} // end namespace mvlc

#endif /* __MVME_MVLC_READOUT_PARSERS_H__ */
