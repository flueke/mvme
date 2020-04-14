/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__
#define __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__

#include <functional>

#include "mesytec-mvlc_export.h"
#include "mvlc_command_builders.h"
#include "mvlc_util.h"

namespace mesytec
{
namespace mvlc
{
namespace readout_parser
{

// Purpose: The reaodut_parser system is used to parse a possibly lossfull
// sequence of MVLC readout buffers into complete readout event data
// and make this data available to a consumer.
//
// StackCommands that produce output:
//   marker         -> one word
//   single_read    -> one word
//   block_read     -> dynamic part (0xF5 framed)
//
// Restrictions applying to the structure of each stack command group:
//
// - an optional fixed size prefix part (single value read and marker commands)
// - an optional dynamic block part (block read commands)
// - an optional fixed size suffix part (single value read and marker commands)


struct GroupReadoutStructure
{
    u8 prefixLen; // length in 32 bit words of the fixed part prefix
    u8 suffixLen; // length in 32 bit words of the fixed part suffix
    bool hasDynamic; // true if a dynamic part (block read) is present
};

inline bool is_empty(const GroupReadoutStructure &mrp)
{
    return mrp.prefixLen == 0 && mrp.suffixLen == 0 && !mrp.hasDynamic;
}

struct Span
{
    u32 offset;
    u32 size;
};

struct GroupReadoutSpans
{
    Span prefixSpan;
    Span dynamicSpan;
    Span suffixSpan;
};

inline bool is_empty(const GroupReadoutSpans &spans)
{
    return (spans.prefixSpan.size == 0
            && spans.dynamicSpan.size == 0
            && spans.suffixSpan.size == 0);
}

struct end_of_frame: public std::exception {};

struct ReadoutParserCallbacks
{
    // functions taking an event index
    std::function<void (int ei)>
        beginEvent = [] (int) {},
        endEvent   = [] (int) {};

    // Parameters: event index, module index, pointer to first word, number of words
    std::function<void (int ei, int mi, const u32 *data, u32 size)>
        modulePrefix  = [] (int, int, const u32*, u32) {},
        moduleDynamic = [] (int, int, const u32*, u32) {},
        moduleSuffix  = [] (int, int, const u32*, u32) {};

    // Parameters: pointer to first word of the system event data, number of words
    std::function<void (const u32 *header, u32 size)>
        systemEvent = [] (const u32 *, u32) {};
};

enum class ParseResult
{
    Ok,
    NoHeaderPresent,
    NoStackFrameFound,

    NotAStackFrame,
    NotABlockFrame,
    NotAStackContinuation,
    StackIndexChanged,
    StackIndexOutOfRange,
    GroupIndexOutOfRange,
    EmptyStackFrame,
    UnexpectedOpenBlockFrame,

    // IMPORTANT: These should not happen and be fixed in the code if they
    // happen. They indicate that the parser algorithm did not advance through
    // the buffer but is stuck in place, parsing the same data again.
    ParseReadoutContentsNotAdvancing,
    ParseEthBufferNotAdvancing,
    ParseEthPacketNotAdvancing,

    ParseResultMax
};

MESYTEC_MVLC_EXPORT const char *get_parse_result_name(const ParseResult &pr);

struct MESYTEC_MVLC_EXPORT ReadoutParserCounters
{
    u32 internalBufferLoss;
    u32 buffersProcessed;
    u64 unusedBytes;

    u32 ethPacketLoss;
    u32 ethPacketsProcessed;

    std::array<u32, system_event::subtype::SubtypeMax + 1> systemEventTypes;

    using ParseResultArray = std::array<u32, static_cast<size_t>(ParseResult::ParseResultMax)>;
    ParseResultArray parseResults;
    u32 parserExceptions;
};

struct MESYTEC_MVLC_EXPORT ReadoutParserState
{
    // Helper structure keeping track of the number of words left in a MVLC
    // style data frame.
    struct FrameParseState
    {
        FrameParseState(u32 frameHeader = 0)
            : header(frameHeader)
            , wordsLeft(extract_frame_info(frameHeader).len)
        {}

        inline explicit operator bool() const { return wordsLeft; }
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

    using ReadoutStructure = std::vector<std::vector<GroupReadoutStructure>>;

    struct WorkBuffer
    {
        std::vector<u32> buffer;
        size_t used = 0;

        inline size_t free() const { return buffer.size() - used; }
    };

    // The readout workers start with buffer number 1 so buffer 0 can only
    // occur after wrapping the counter. By using 0 as a starting value the
    // buffer loss calculation will work without special cases.
    u32 lastBufferNumber = 0;

    // Space to assemble linear readout data.
    WorkBuffer workBuffer;

    // Per module offsets and sizes into the workbuffer. This is a map of the
    // current layout of the workbuffer.
    std::vector<GroupReadoutSpans> readoutDataSpans;

    // Per event preparsed module readout info.
    ReadoutStructure readoutStructure;

    int eventIndex = -1;
    int moduleIndex = -1;
    ModuleParseState moduleParseState = Prefix;

    // Parsing state of the current 0xF3 stack frame. This is always active
    // when parsing readout data.
    FrameParseState curStackFrame = {};

    // Parsing state of the current 0xF5 block readout frame. This is only
    // active when parsing the dynamic part of a module readout.
    FrameParseState curBlockFrame = {};

    // ETH parsing only. The transmitted packet number type is u16. Using an
    // s32 here to represent the "no previous packet" case by storing a -1.
    s32 lastPacketNumber = -1;

    ReadoutParserCounters counters = {};
};

// Create a readout parser from a list of readout stack defintions.
//
// This function assumes that the first element in the vector contains the
// definition for the readout stack with id 1, the second the one for stack id
// 2 and so on. Stack 0 (the direct exec stack) must not be included.
MESYTEC_MVLC_EXPORT ReadoutParserState make_readout_parser(
    const std::vector<StackCommandBuilder> &readoutStacks);

// Functions for steering the parser. These should be called repeatedly with
// complete MVLC readout buffers. The input buffer sequence may be lossfull
// which is useful when snooping parts of the data during a DAQ run.

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer(
    ConnectionType bufferType,
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_eth(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

MESYTEC_MVLC_EXPORT ParseResult parse_readout_buffer_usb(
    ReadoutParserState &state,
    ReadoutParserCallbacks &callbacks,
    u32 bufferNumber, const u32 *buffer, size_t bufferWords);

} // end namespace readout_parser
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_PARSER_H__ */
