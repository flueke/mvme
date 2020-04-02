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
#ifndef __MESYTEC_MVLC_MVLC_CONSTANTS_H__
#define __MESYTEC_MVLC_MVLC_CONSTANTS_H__

#include <cstddef>
#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{
// Communication with the MVLC is done using 32-bit wide binary data words.
// Results from commands and stack executions are also 32-bit aligned.
// All data is in little-endian byte order.

static const u32 AddressIncrement = 4;
static const u32 ReadLocalBlockMaxWords = 768;
static const u32 FrameSizeMask = 0xFFFF;

// Super commands are commands that are directly interpreted and executed
// by the MVLC.
// The values in the SuperCommands enum contain the 2 high bytes of the
// command word.
// The output of super commands always goes to pipe 0, the CommandPipe.
namespace super_commands
{
    static const u32 SuperCmdMask  = 0xFFFF;
    static const u32 SuperCmdShift = 16;
    static const u32 SuperCmdArgMask = 0xFFFF;
    static const u32 SuperCmdArgShift = 0;

    enum class SuperCommandType: u16
    {
        CmdBufferStart = 0xF100,
        CmdBufferEnd   = 0xF200,
        ReferenceWord  = 0x0101,
        ReadLocal      = 0x0102,
        ReadLocalBlock = 0x0103,
        WriteLocal     = 0x0204,
        WriteReset     = 0x0206,
    };
}

using super_commands::SuperCommandType;

// Stack-only commands. These can be written into the stack memory area
// starting from StackMemoryBegin using WriteLocal commands.
//
// The output produced by a stack execution can go to either the
// CommandPipe or the DataPipe. This is encoded in the StackStart command.

namespace stack_commands
{
    static const u32 CmdMask      = 0xFF;
    static const u32 CmdShift     = 24;
    static const u32 CmdArg0Mask  = 0x00FF;
    static const u32 CmdArg0Shift = 16;
    static const u32 CmdArg1Mask  = 0x0000FFFF;
    static const u32 CmdArg1Shift = 0;

    enum class StackCommandType: u8
    {
        StackStart      = 0xF3,
        StackEnd        = 0xF4,
        VMERead         = 0x12,
        VMEWrite        = 0x23,
        WriteMarker     = 0xC2,
        WriteSpecial    = 0xC1,
    // TODO: ScanDataRead, ReadDataLoop and masks/enums
    //static const u32 ScanDataRead      = 0x34;
    };
}

using stack_commands::StackCommandType;

namespace frame_headers
{
    enum FrameTypes: u8
    {
        SuperFrame        = 0xF1,
        StackFrame        = 0xF3,
        BlockRead         = 0xF5,
        StackError        = 0xF7,
        StackContinuation = 0xF9,
        SystemEvent       = 0xFA,
    };

    // Header: Type[7:0] Continue[0:0] ErrorFlags[2:0] StackNum[3:0] CtrlId[2:0] Length[12:0]
    // The Continue bit and the ErrorFlags are combined into a 4 bit
    // FrameFlags field.

    static const u8 TypeShift           = 24;
    static const u8 TypeMask            = 0xff;

    static const u8 FrameFlagsMask      = 0xf;
    static const u8 FrameFlagsShift     = 20;

    static const u8 StackNumShift       = 16;
    static const u8 StackNumMask        = 0xf;

    static const u8 CtrlIdShift         = 13;
    static const u8 CtrlIdMask          = 0b111;

    static const u16 LengthShift        = 0;
    static const u16 LengthMask         = 0x1fff;
}

inline u8 get_frame_type(u32 header)
{
    return (header >> frame_headers::TypeShift) & frame_headers::TypeMask;
}

namespace frame_flags
{
    namespace shifts
    {
        static const u8 Timeout     = 0;
        static const u8 BusError    = 1;
        static const u8 SyntaxError = 2;
        static const u8 Continue    = 3;
    }

    static const u8 Timeout     = 1u << shifts::Timeout;
    static const u8 BusError    = 1u << shifts::BusError;
    static const u8 SyntaxError = 1u << shifts::SyntaxError;
    static const u8 Continue    = 1u << shifts::Continue;

    static const u8 AllErrorFlags = (
        frame_flags::Timeout |
        frame_flags::BusError |
        frame_flags::SyntaxError);
}

// Software generated system events which do not collide with the MVLCs framing
// format.
namespace system_event
{
    // TTTT TTTT CUUU SSSS SSSL LLLL LLLL LLLL
    // Type     [ 7:0] set to 0xFA
    // Continue [ 0:0] continue bit set for all but the last part
    // Unused   [ 2:0] 3 unused flag bits
    // Subtype  [ 6:0] 7 bit system event SubType
    // Length   [12:0] 13 bit length counted in 32-bit words

    static const u8 ContinueShift = 23;
    static const u8 ContinueMask  = 0b1;

    static const u8 SubtypeShift  = 13;
    static const u8 SubtypeMask   = 0x7f;

    static const u16 LengthShift  = 0;
    static const u16 LengthMask   = 0x1fff;

    static const u32 EndianMarkerValue = 0x12345678u;

    namespace subtype
    {
        static const u8 EndianMarker    = 0x01;

        static const u8 VMEConfig       = 0x10;
        static const u8 UnixTimestamp   = 0x11;
        static const u8 Pause           = 0x12;
        static const u8 Resume          = 0x13;

        static const u8 EndOfFile       = 0x77;

        static const u8 SubtypeMax      = SubtypeMask;
    };

    inline u8 extract_subtype(u32 header)
    {
        return (header >> SubtypeShift) & SubtypeMask;
    }

    inline bool is_known_subtype(u8 st)
    {
        return (st == subtype::EndianMarker
                || st == subtype::VMEConfig
                || st == subtype::UnixTimestamp
                || st == subtype::Pause
                || st == subtype::Resume
                || st == subtype::EndOfFile);
    }

    inline bool is_known_system_event(u32 header)
    {
        return get_frame_type(header) == frame_headers::SystemEvent
            && is_known_subtype(extract_subtype(header));
    }
}

enum class VMEDataWidth
{
    D16 = 0x1,
    D32 = 0x2
};

enum class Blk2eSSTRate: u8
{
    Rate160MB,
    Rate276MB,
    Rate300MB,
};

// Shift relative to the AddressMode argument of the read.
static const u8 Blk2eSSTRateShift = 6;

// For the WriteSpecial command
enum class SpecialWord: u8
{
    Timestamp,
    StackTriggers
};

static const u16 InternalRegisterMin = 0x0001;
static const u16 InternalRegisterMax = 0x5FFF;

// Setting bit 0 to 1 enables autonomous execution of stacks in
// reaction to triggers.
// IMPORTANT: This is always active right now, meaning as soon as an individual
// stack trigger register is written the triggers will be processed.
static const u32 DAQModeEnableRegister = 0x1300;

namespace stacks
{
    static const u8 StackCount = 8;
    static const u16 Stack0TriggerRegister = 0x1100;

    // Note: The stack offset registers take offsets from StackMemoryBegin, not
    // absolute memory addresses. The offsets are counted in bytes, not words.
    static const u16 Stack0OffsetRegister  = 0x1200;

    static const u16 StackMemoryBegin      = 0x2000;
    static const u16 StackMemoryWords      = 1024;
    static const u16 StackMemoryBytes      = StackMemoryWords * 4;
    static const u16 StackMemoryEnd        = StackMemoryBegin + StackMemoryBytes;

    // Mask for the number of valid bits in the stack offset register.
    // Higher order bits outside the mask are ignored by the MVLC.
    static const u16 StackOffsetBitMaskWords    = 0x03FF;
    static const u16 StackOffsetBitMaskBytes    = StackOffsetBitMaskWords * 4;

    // The stack used for immediate execution, e.g for directly writing a VME
    // device register. This is a software-side convention only, hardware wise
    // nothing special is going on.
    static const u8 ImmediateStackID = 0;
    static const u16 ImmediateStackReservedWords = 128;
    static const u16 ImmediateStackReservedBytes = ImmediateStackReservedWords * 4;
    static const u8 FirstReadoutStackID = 1;

    enum TriggerType: u8
    {
        NoTrigger   = 0,
        IRQWithIACK = 1,
        IRQNoIACK   = 2,
        External    = 3,
    };

    // Note: For IRQ triggers the TriggerBits have to be set to the value
    // (IRQ-1), e.g. value 0 for IRQ1! Higher IRQ numbers have higher priority.
    static const u16 TriggerBitsMask    = 0b11111;
    static const u16 TriggerBitsShift   = 0;
    static const u16 TriggerTypeMask    = 0b111;
    static const u16 TriggerTypeShift   = 5;
    static const u16 ImmediateMask      = 0b1;
    static const u16 ImmediateShift     = 8;

    inline u16 get_trigger_register(u8 stackId)
    {
        return Stack0TriggerRegister + stackId * AddressIncrement;
    }

    inline u16 get_offset_register(u8 stackId)
    {
        return Stack0OffsetRegister + stackId * AddressIncrement;
    }

    static const u16 TimerCount = 4;
    static const u16 TimerPeriodMin_ns = 16;
    static const u16 TimerPeriodMax = 0xffff;

    enum class TimerBaseUnit: u16
    {
        ns,
        us,
        ms,
        s
    };
} // end namespace stacks

constexpr const u32 SelfVMEAddress = 0xFFFF0000u;

namespace usb
{
    // Limit imposed by FT_WritePipeEx and FT_ReadPipeEx
    static const size_t USBSingleTransferMaxBytes = 1 * 1024 * 1024;
    static const size_t USBSingleTransferMaxWords = USBSingleTransferMaxBytes / sizeof(u32);
} // end namespace usb

namespace eth
{
    static const u16 CommandPort = 0x8000; // 32768
    static const u16 DataPort = CommandPort + 1;
    static const u32 HeaderWords = 2;
    static const u32 HeaderBytes = HeaderWords * sizeof(u32);

    namespace header0
    {
        // 2 bit packet channel number. Values represent different streams of
        // data each with its own packet number counter (see PacketChannel enum
        // below).
        static const u32 PacketChannelMask  = 0b11;
        static const u32 PacketChannelShift = 28;

        // 12 bit packet number
        // Packet channel specific incrementing packet number.
        static const u32 PacketNumberMask  = 0xfff;
        static const u32 PacketNumberShift = 16;

        // 3 Reserved Bits

        // 13 bit number of data words
        // This is the number of data words following the two header words.
        static const u32 NumDataWordsMask  = 0x1fff;
        static const u32 NumDataWordsShift = 0;
    }

    namespace header1
    {
        // 20 bit ETH timestamp. Increments in 1ms steps. Wraps after 17.5
        // minutes.
        static const u32 TimestampMask      = 0xfffff;
        static const u32 TimestampShift     = 12;

        // Points to the next buffer header word in the packet data. The
        // position directly after this header1 word is 0.
        // The maximum value possible indicates that there's no buffer header
        // present in the packet data. This means the packet must contain
        // continuation data from a previously started buffer.
        // This header pointer value can be used to resume processing data
        // packets in case of packet loss.
        static const u32 HeaderPointerMask  = 0xfff;
        static const u32 HeaderPointerShift = 0;
        static const u32 NoHeaderPointerPresent = HeaderPointerMask;
    }

    static const size_t JumboFrameMaxSize = 9000;

    enum class PacketChannel: u8
    {
        Command, // Command and mirror responses
        Stack,   // Data produced by stack executions routed to the command pipe
        Data,    // Readout data produced by stacks routed to the data pipe
    };

    static const u8 NumPacketChannels = 3;

} // end namespace eth

namespace registers
{
    // Send gap for USB in 0.415us. Defaults to 20000 == 8.3ms
    static const u16 usb_send_gap           = 0x0400;

    static const u16 own_ip_lo              = 0x4400;
    static const u16 own_ip_hi              = 0x4402;
    static const u16 StoreIPInFlash         = 0x4404;

    static const u16 dhcp_active            = 0x4406; // 0 = fixed IP, 1 = DHCP
    static const u16 dhcp_ip_lo             = 0x4408;
    static const u16 dhcp_ip_hi             = 0x440a;

    static const u16 cmd_ip_lo              = 0x440c;
    static const u16 cmd_ip_hi              = 0x440e;

    static const u16 data_ip_lo             = 0x4410;
    static const u16 data_ip_hi             = 0x4412;

    static const u16 cmd_mac_0              = 0x4414;
    static const u16 cmd_mac_1              = 0x4416;
    static const u16 cmd_mac_2              = 0x4418;

    static const u16 cmd_dest_port          = 0x441a;
    static const u16 data_dest_port         = 0x441c;

    static const u16 data_mac_0             = 0x441e;
    static const u16 data_mac_1             = 0x4420;
    static const u16 data_mac_2             = 0x4422;

    static const u16 crc_good_ctr           = 0x4424;
    static const u16 crc_bad_ctr            = 0x4426;
    static const u16 skip_receive_frame_ctr = 0x4428;
    static const u16 receive_arp_ctr        = 0x442a;
    static const u16 receive_ping_ctr       = 0x442c;
    static const u16 receive_datin_ctr      = 0x442e;
    static const u16 receive_cmdin_ctr      = 0x4430;

    static const u16 arp_sender_mac_rx_0    = 0x4432;
    static const u16 arp_sender_mac_rx_1    = 0x4434;
    static const u16 arp_sender_mac_rx_2    = 0x4436;

    static const u16 arp_sender_ip_rx_lo    = 0x4438;
    static const u16 arp_sender_ip_rx_hi    = 0x443a;

    static const u16 reset_register_mask    = 0x0202;
    static const u16 reset_register         = 0x6090;
    static const u16 hardware_id            = 0x6008;
    static const u16 firmware_revision      = 0x600e;
    static const u16 mcst_enable            = 0x6020;
    static const u16 mcst_address           = 0x6024;
} // end namespace registers

static const u8 CommandPipe = 0;
static const u8 DataPipe = 1;
static const unsigned PipeCount = 2;

enum class Pipe: u8
{
    Command = CommandPipe,
    Data = DataPipe,
};

// Note: The write timeout applies to USB only. When using ethernet the sockets
// are set to blocking mode in the outgoing direction.
static const unsigned DefaultWriteTimeout_ms = 500;
static const unsigned DefaultReadTimeout_ms  = 500;

enum class ConnectionType
{
    USB,
    ETH
};

namespace stack_error_info
{
    static const unsigned StackLineMask = 0xffffu;
    static const unsigned StackLineShift = 0u;
    static const unsigned StackNumberMask = 0xffffu;
    static const unsigned StackNumberShift = 16u;
}

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_CONSTANTS_H__ */
