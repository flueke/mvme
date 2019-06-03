#ifndef __MVME_MVLC_READOUT_PARSERS_H__
#define __MVME_MVLC_READOUT_PARSERS_H__

#include "vme_script.h"
#include "mvlc/mvlc_impl_eth.h"
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

using VMEConfReadoutScripts = std::vector<std::vector<vme_script::VMEScript>>;
using VMEConfReadoutInfo    = std::vector<std::vector<ModuleReadoutParts>>;

VMEConfReadoutInfo parse_event_readout_info(const VMEConfReadoutScripts &rdoScripts);

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

struct ReadoutParser_ETH
{
    // The readout workers start with buffer number 1 so buffer 0 can only
    // occur after wrapping the counter but by using 0 as a starting value the
    // loss calculation will be correct in all cases.
    u32 lastBufferNumber = 0;

    // The actual packet number is of type u16. Using an s32 here to represent
    // the "no previous packet" case by storing a -1.
    s32 lastPacketNumber = -1;

    eth::PayloadHeaderInfo payloadInfo;
    DataBuffer workBuffer;

    VMEConfReadoutInfo readoutInfo;
    std::vector<ModuleReadoutSpans> readoutDataSpans;
    int eventIndex = -1;
    int moduleIndex = -1;
    enum ModuleParseState { Prefix, Dynamic, Suffix };
    ModuleParseState moduleParseState;
};

struct ReadoutParserCallbacks
{
#if 0
    using BeginEvent = void (*) (int ei);
    using ModuleData = void (*) (int ei, int mi, u32 *data, u32 size);
    using EndEvent   = void (*) (int ei);

    BeginEvent beginEvent;
    ModuleData moduleData;
    EndEvent endEvent;
#else
    std::function<void (int ei)> beginEvent;
    std::function<void (int ei, int mi, u32 *data, u32 size)> moduleData;
    std::function<void (int ei)> endEvent;
#endif
};

} // end namespace mesytec
} // end namespace mvlc

#endif /* __MVME_MVLC_READOUT_PARSERS_H__ */
