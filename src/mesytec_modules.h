#ifndef UUID_5c5a314b_81fc_4423_bbcc_3068085e2815
#define UUID_5c5a314b_81fc_4423_bbcc_3068085e2815


/*
MDPP16
    Header 01 00
        8   module_id
        3   tdc_resolution
        3   adc_resoltion
        10  n_words


    The full channel address uses trigger flag and channel number to form a 6 bit
    channel address running from 0 to 33. 0-15 is for amplitudes, 16-31 for
    time and 32/33 for trigger0/trigger1 times.

    Data 00 01
        1   pileup
        1   overflow
        1   trigger flag
        5   channel number
        16  adc value for channel address = [0,15]

    Data 00 01
        1   trigger flag
        5   channel number
        16  tdc time difference for channel address = [16,33]

    Fill 00 00
    Extended Timestamp 0010
    End of Event 11
        30 counter/timestamp

        
*/

struct DataWord
{
    bool isValid();
    int address();  // channel number / combined address (for mdpp16)
    u32 value();    // adc/tdc value
    bool overflow();
    bool pileup();
    int bus(); // for mdi-2

    ModuleData module; // to get type() and other module info
    u32 *data = 0; // pointer the to actual data word
};

struct ModuleData
{
    ModuleData(u32 *subEventHeader, u32 ptrSizeInWords);

    bool isValid();
    VMEModuleType type();
    int sizeInWords();
    int headerLength();
    int headerModuleID();
    int headerADCResolution();
    int headerTDCResolution();

    int dataLength();
    DataWord data(int index);

    bool hasExtendedTimestamp();
    u32 extendedTimestampValue();

    bool hasEOE();
    u32 eoeValue();

    u32 *subEventHeader = 0; // points to the MVME subevent header
    u32 ptrSizeInWords = 0;
};


// XXX: The event can contain any kind of modules, not just mesytec modules...
struct EventData
{
    EventData(u32 *eventHeader);

    // event type as defined in the daq config
    int type();
    // total size of the event in 32 bit words, excluding the event header // itself
    int sizeInWords();

    int moduleCount();
    ModuleData module(int index);

    u32 *eventHeader = 0; // points to the MVME Event header
};

#endif
