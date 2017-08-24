#!/usr/bin/env python

from __future__ import print_function

# Note: Module specific details are missing, e.g Pileup, Underflow, Overflow...

import sys

HeaderMask          = 0xC0000000
HeaderResult        = 0x40000000
HeaderModuleIdMask  = 0x0F000000
HeaderModuleIdShift = 24
HeaderLengthMask    = 0x00000FFF

DataMaskMDPP        = 0xF0000000 # Data marker bits for MDPP
DataResultMDPP      = 0x10000000
DataMaskMxDC        = 0xFF800000 # Data marker bits for MxDC
DataResultMxDC      = 0x04000000
ChannelExtractMask  = 0x003F0000 # 6-bit address mask
ChannelExtractShift = 16
DataExtractMask     = 0x0000FFFF # 16 bit data mask (MQDC only has 13-bits!)

ExtTsMask           = 0xFF800000 # extended timestamp
ExtTsResult         = 0x04800000
ExtTsStampMask      = 0x0000FFFF

EoEMask             = 0xC0000000 # End Of Event
EoEResult           = 0xC0000000
EoECounterMask      = 0x3FFFFFFF


# huh? our fillword is 0x00000000
#FillWord     = 0x00800000


# if no arg is given: read from stdin
# if args are given: read each arg as a 32 bit word

def handle_one_word(dataWord, wordIndex):
    if dataWord == 0xFFFFFFFF:
        print("%4d: 0x%08X => BERR" % (wordIndex, dataWord))
        return

    headerFound = (dataWord & HeaderMask) == HeaderResult

    dataFound   = (((dataWord & DataMaskMDPP) == DataResultMDPP)
            or ((dataWord & DataMaskMxDC) == DataResultMxDC))

    eoeFound    = (dataWord & EoEMask) == EoEResult
    extTsFound  = (dataWord & ExtTsMask) == ExtTsResult

    if headerFound:
        moduleId = (dataWord & HeaderModuleIdMask) >> HeaderModuleIdShift
        dataLength = (dataWord & HeaderLengthMask)
        print("%4d: 0x%08X => Header" % (wordIndex, dataWord))
        print("\tmoduleId   = %d" % moduleId)
        print("\t#dataWords = %d" % dataLength)

    elif dataFound:
        channel = (dataWord & ChannelExtractMask) >> ChannelExtractShift
        data    = (dataWord & DataExtractMask)
        print("%4d: 0x%08X => Data" % (wordIndex, dataWord))
        print("\tChannel = %2d" % channel)
        print("\tData    = %d" % data)

    elif eoeFound:
        counter = (dataWord & EoECounterMask)
        print("%4d: 0x%08X => End of Event" % (wordIndex, dataWord))
        print("\tCounter/TS low = %d" % counter)

    elif extTsFound:
        stamp = (dataWord & ExtTsStampMask)
        print("%4d: 0x%08X => Extended Timestamp" % (wordIndex, dataWord))
        print("\tTS high = %d" % stamp)
    elif dataWord == 0x0:
        print("%4d: 0x%08X => Fillword" % (wordIndex, dataWord))

    else:
        print("%4d: 0x%08X => No mask matched, not a valid mesytec data word!" % (wordIndex, dataWord))


if __name__ == "__main__":
    wordIndex = 0
    dataSource = None

    if len(sys.argv) > 1:
        # Use each arg as a data word
        dataSource = sys.argv[1:]
    else:
        print("Reading from stdin. Type Ctrl-D to quit.", file=sys.stderr)
        dataSource = sys.stdin

    for data in dataSource:
        dataWord = int(data, 0)
        handle_one_word(dataWord, wordIndex)
        wordIndex += 1

    sys.exit(0)
