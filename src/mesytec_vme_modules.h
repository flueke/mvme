#ifndef _SRC_MESYTEC_VME_MODULES_H_
#define _SRC_MESYTEC_VME_MODULES_H_

#include "typedefs.h"

namespace mesytec::vme_modules
{

// Full 16 bit values of the hardware id register (0x6008).
struct HardwareIds
{
    u16 MADC_32 = 0x5002;
    u16 MQDC_32 = 0x5003;
    u16 MTDC_32 = 0x5004;
    u16 MDPP_16 = 0x5005;
    // The VMMRs use the exact same software, so the hardware ids are equal.
    // VMMR-8 is a VMMR-16 with 8 busses missing.
    u16 VMMR_8  = 0x5006;
    u16 VMMR_16 = 0x5006;
    u16 MDPP_32 = 0x5007;
};

// Firmware type is encoded in the highest nibble of the firmware register
// (0x600e). The lower nibbles contain the firmware revision. Valid for both
// MDPP-16 and MDPP-32 but not all packages exist for the MDPP-32.
struct MDPP_FirmwareTypes
{
    u16 PADC = 0;
    u16 SCP  = 1;
    u16 RCP  = 2;
    u16 QDC  = 3;
};

}

#endif // _SRC_MESYTEC_VME_MODULES_H_