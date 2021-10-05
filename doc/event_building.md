Timestamp based, cross-crate, event building
============================================

The reference/main module in each event is marked with (**). This module must
produce data in every readout cycle, meaning the module is present every time
the parent event is triggered and read out.

Crate centric view
------------------

- Crate0                - Crate1                - Crate2
  - CrateConfig0          - CrateConfig1          - CrateConfig2
  - event00               - event10               - event20
    - module000 (**)        - module100             - module200
    - module001             - module101             - module201
    - module002             - module102             - module202
  - event01               - event11               - event21
    - module010             - module110 (**)        - module210
    - module011             - module111             - module211
    - module012             - module112             - module212


Event centric view
------------------

- event0                - event1
  - module000 (**)        - module010
  - module001             - module011
  - module002             - module012
  - module100             - module110 (**)
  - module101             - module111
  - module102             - module112
  - module200             - module210
  - module201             - module211
  - module202             - module212

Preparation
-----------

Each module needs a timestamp extractor, preferably one that takes O(1) time.

For each module define an acceptable timestamp range relative to the events
main module, e.g. [-500, +1000].

For each full event for which event building is enabled create a build area.
The build area needs one event buffer per module and an index structure to be
able quickly jump to each event in the module buffer. Timestamps could be
cached to avoid having to extract them multiple times.

Execution
---------

Let the data from each crate be processed by a readout parser instance. Copy
the yielded module data to the build area for the corresponding full event.

Once a certain minimum number of partial events have been collected for each of     <-  Number of events or minimum timestamp range.
the modules in the full event, start the event building process:                        How to find reasonable values? By looking at
                                                                                        the allowed timestamp ranges.
Start with the timestamp from the main module. Add the main modules event to
the output event. For each module in the full event find the first event that
falls withing the acceptable time range. Possible cases:

  - event stamp is earlier:
    Skip the event and continue searching in the following events.

  - event stamp is later:
    Set this modules event data to null in the output event.

  - event stamp falls in acceptable range:
    This event is part of the full output event. Remove it from the buffer.
