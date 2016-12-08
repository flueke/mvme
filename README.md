# mvme2 test version

## Windows USB drivers

libusb-win32 is needed for VM_USB connectivity. The driver is included in the
archive. To install the driver run *libusb-win32-bin-1.2.6.0/bin/inf-wizard.exe*
to create and install the filter driver for your VM_USB. The VM_USB should now
show up under "libusb-win32 devices" in the Windows device manager.

## Listfile format

The listfile is divided into sections of different types. The first sections
(at least one) in a listfile are of type "Config" and contain the experiment
configuration that was used to produce the listfile encoded as a JSON string.

The following sections of type "Event" contain the actual event data.

The last section of a listfile is an empty section of type "End".

All header words and module data words are 32-bit aligned in little endian byte
order.  16-bit module data is zero-padded to fill a 32-bit word.

Listfile Structure:

    Config Section Header
        JSON encoded config file contents

    Optional additional config sections

    Event Section Header, contains event type id
        Subevent Header 0, contains the module type
            Module Contents
        EndMarker

        Subevent Header 1, contains the module type
            Module Contents
        EndMarker

    EndMarker (marks the end of the Event section)

    Additional Event Sections
    ...

    End Section

Currently defined section types:

* SectionType_Config = 0
* SectionType_Event  = 1
* SectionType_End    = 2


Section Header Word:

    +----- Section Header Word ------+
    |33222222222211111111110000000000|
    |10987654321098765432109876543210|
    +--------------------------------+
    |ttt         eeeessssssssssssssss|
    +--------------------------------+
    
    t =  3 bit Section type
    e =  4 bit Event type (== event number/index) for event sections.
               Use this to look up the EventConfig in the config section.
    s = 16 bit Size in units of 32 bit words (fillwords added to data if needed)
               -> 256k section max size
               Section size is the number of following 32 bit words not including
               the header word itself.

Sections with SectionType_Event contain subevents with the following header:

Subevent Header Word:

    +------- Subevent Header --------+
    |33222222222211111111110000000000|
    |10987654321098765432109876543210|
    +--------------------------------+
    |              mmmmmm  ssssssssss|
    +--------------------------------+
    
    m =  6 bit module type (VMEModuleType enum from globals.h)
    s = 10 bit size in units of 32 bit words

Currently defined module types:

* MADC32  = 1
* MQDC32  = 2
* MTDC32  = 3
* MDPP16  = 4
* MDPP32  = 5
* MDI2    = 6
* Generic = 48
 
The last word of event and subevent sections is the EndMarker (0x87654321).

# Libraries and 3rd-party code
* http://qwt.sourceforge.net/
