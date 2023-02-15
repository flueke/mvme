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
#ifndef __SIS3153_H__
#define __SIS3153_H__

#include "libmvme_export.h"
#include "vme_controller.h"

class sis3153eth;
struct SIS3153Private;

class LIBMVME_EXPORT SIS3153: public VMEController
{
    Q_OBJECT

    public:
        SIS3153(QObject *parent = 0);
        virtual ~SIS3153();

        //
        // VMEController implementation
        //
        virtual bool isOpen() const;
        virtual VMEError open();
        virtual VMEError close();
        virtual ControllerState getState() const;
        virtual QString getIdentifyingString() const;
        virtual VMEControllerType getType() const;

        virtual VMEError write32(u32 address, u32 value, u8 amod);
        virtual VMEError write16(u32 address, u16 value, u8 amod);

        virtual VMEError read32(u32 address, u32 *value, u8 amod);
        virtual VMEError read16(u32 address, u16 *value, u8 amod);

        virtual VMEError blockRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo);

        //
        // SIS3153 specific
        //

        /* Note: do not keep copies of these pointers around. They will be
         * invalidated when reconnecting!. */
        sis3153eth *getImpl();
        sis3153eth *getCtrlImpl();

        // Set IP address or hostname to connect to. Takes effect after closing
        // and reopening the device.
        void setAddress(const QString &address);
        QString getAddress() const;

        VMEError readRegister(u32 address, u32 *outValue);
        VMEError writeRegister(u32 address, u32 value);

        void setResetOnConnect(bool sendReset);
        bool doesResetOnConnect() const;

    private:
        SIS3153Private *m_d;
};

namespace SIS3153Registers
{
    static const u32 USBControlAndStatus        = 0x0;

    namespace USBControlAndStatusValues
    {
        static const u32 DisableShift           = 16;
        static const u32 LED_A                  = 1 << 0;
    }

    static const u32 ModuleIdAndFirmware        = 0x1;
    static const u32 SerialNumber               = 0x2;
    static const u32 LemoIOControl              = 0x3;

    namespace LemoIOControlValues
    {
        /* The bits below are used to enable a certain setting. Shifting a bit
         * by DisableShift and writing it to the register disables the setting.
         */
        static const u32 DisableShift = 16;

        /* High level control of the outputs. The choice between NIM and TTL
         * should be made via the onboard jumpbers. */
        static const u32 OUT1 = 1u << 4;
        static const u32 OUT2 = 1u << 5;
    }

    static const u32 UDPConfiguration           = 0x4;

    namespace UDPConfigurationValues
    {
        static const u32 GapTimeMask             = 0xfu;
        static const u32 GapTimeValueCount       = GapTimeMask + 1;

        static const QString GapTimeValues[GapTimeValueCount] =
        {
            "256 ns",
            "512 ns",
            "1 us",
            "2 us",
            "4 us",
            "8 us",
            "10 us",
            "12 us",
            "14 us",
            "16 us",
            "20 us",
            "28 us",
            "32 us",
            "41 us",
            "50 us",
            "57 us",
        };
    }

    static const u32 VMEMasterStatusAndControl  = 0x10;
    static const u32 VMEMasterCycleStatus       = 0x11;
    static const u32 VMEInterruptStatus         = 0x12;

    static const u32 KeyResetAll                = 0x100;

    static const u32 StackListConfig1           = 0x01000000;
    static const u32 StackListTrigger1          = 0x01000001;
    static const u32 StackListConfig2           = 0x01000002;
    static const u32 StackListTrigger2          = 0x01000003;
    static const u32 StackListConfig3           = 0x01000004;
    static const u32 StackListTrigger3          = 0x01000005;
    static const u32 StackListConfig4           = 0x01000006;
    static const u32 StackListTrigger4          = 0x01000007;
    static const u32 StackListConfig5           = 0x01000008;
    static const u32 StackListTrigger5          = 0x01000009;
    static const u32 StackListConfig6           = 0x0100000a;
    static const u32 StackListTrigger6          = 0x0100000b;
    static const u32 StackListConfig7           = 0x0100000c;
    static const u32 StackListTrigger7          = 0x0100000d;
    static const u32 StackListConfig8           = 0x0100000e;
    static const u32 StackListTrigger8          = 0x0100000f;

    static const u32 StackListControl               = 0x01000010;
    static const u32 StackListTriggerCommand        = 0x01000011;
    static const u32 StackListShortPackageLength    = 0x01000012;

    static const u32 StackListTimer1Config          = 0x01000014;
    static const u32 StackListTimer2Config          = 0x01000015;

    // Configuration register for dynamically sized block reads.
    static const u32 StackListDynSizedBlockRead     = 0x01000016;

    // Test readback register for dynamic block reads. Will contain the last
    // dynamic block size in bytes.
    static const u32 StackListDynSizeReadback       = 0x01000017;

    namespace StackListControlValues
    {
        /* Writing a 1 to the low 16 bits of the StackListControl register
         * enables the setting. Writing a 1 shifted by DisableShift disables
         * the setting. Doing both at the same time is invalid. */
        static const u32 DisableShift       = 16;

        static const u32 StackListEnable    = 1 << 0;
        static const u32 Timer1Enable       = 1 << 1;
        static const u32 Timer2Enable       = 1 << 2;

        static const u32 DisableBerrStatus  = 1 << 11; // If set the controller will not output a status
                                                       // word (0x02110211) into the data stream in case
                                                       // a read operation results in an immediate BERR without
                                                       // any data. Since firmware 16.A6.

        static const u32 FlushBufferEnable  = 1 << 12; // "Force to send rest of buffer enable" in the manual
        static const u32 ListBufferEnable   = 1 << 15; // "List Multi Event Buffering Enable" in the manual

        /* When reading the register bits 16 to 27 contain the buffer word count.
         * FIXME: figure out what this actually contains. */
        static const u32 BufferWordCountShift = 16;
        static const u32 BufferWordCountMask  = 0xfff;
    }

    static const u32 StackListTimerWatchdogEnable   = 1u << 31;

    static const u32 TriggerSourceTimer1            = 8;
    static const u32 TriggerSourceTimer2            = 9;
    static const u32 TriggerSourceInput1RisingEdge  = 0xC;
    static const u32 TriggerSourceInput1FallingEdge = 0xD;
    static const u32 TriggerSourceInput2RisingEdge  = 0xE;
    static const u32 TriggerSourceInput2FallingEdge = 0xF;

    static const u32 StackListTriggerCommandFlushBuffer = 15;

} // namespace SIS3153Registers

namespace SIS3153Constants
{
    // The packetAck byte value in the case of a buffered multievent packet.
    static const u8 MultiEventPacketAck = 0x60;
    // Mask the Ack with this to obtain the stacklist number.
    static const u8 AckStackListMask    = 0x7;
    // Extract the lastPacket bit from an Ack
    static const u8 AckIsLastPacketMask = 0x8;

    // Masks and results for the two special words added by SIS3153 at the
    // beginning and end of module data.
    static const u32 BeginEventMask   = 0xff000000;
    static const u32 BeginEventResult = 0xbb000000;
    static const u32 EndEventMask     = 0xff000000;
    static const u32 EndEventResult   = 0xee000000;
    static const u32 BeginEventSequenceNumberMask = 0x00ffffff;

    static const u32 EndEventBerrBlockMask = 0x00ff0000;
    static const u32 EndEventBerrReadMask  = 0x0000ff00;
    static const u32 EndEventBerrWriteMask = 0x000000ff;

    static const u32 EndEventBerrBlockShift = 16u;
    static const u32 EndEventBerrReadShift  = 8u;
    static const u32 EndEventBerrWriteShift = 0u;

    static const int NumberOfStackLists = 8;

    static const u32 BlockTransferMaxBytes = 0xffffff;
    static const u32 BLTMaxTransferCount   = BlockTransferMaxBytes / sizeof(u32);
    static const u32 MBLTMaxTransferCount  = BlockTransferMaxBytes / sizeof(u64);

} // namespace SIS3153Constants



void LIBMVME_EXPORT dump_registers(SIS3153 *sis, std::function<void (const QString &)> printer);

VMEError make_sis_error(int sisCode);

#endif /* __SIS3153_H__ */
