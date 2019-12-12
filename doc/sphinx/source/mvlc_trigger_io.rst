
.. highlight:: none

.. _mvlc-trigger-io:

MVLC Trigger and I/O setup
==================================================
What it is. How it is configured by mvme. What mvme reserves.

User Interface
Unit reference
Examples



Introduction
------------
The MVLC VME controller contains a digitial logic module enabling flexible
configuration of readout trigger logic and timings. The module includes setup
of the front-panel NIM I/Os and the ECL output, internal logic functions,
routing and access to utilities like timers, counters and the VME system clock.

.. TODO: add information about internal clocking and processing (maybe)

I/O and logic units
-------------------

NIM I/Os
~~~~~~~~
The front panel NIM connectors can be configured as either input or output.
This means they are available both on the level0 input side and on the level3
output side.

Settings
^^^^^^^^
* Delay
* Width
* Holdoff
* Invert

.. TODO: minmax values and units everywhere.
.. TODO: document behaviour of the invert flag

ECL outputs
~~~~~~~~~~~
These are similar to the NIM output units. Each of the 3 outputs needs to be
activated separately.

Timers
~~~~~~
Generate logic pulses with a set frequency.

.. TODO: minmax values
.. TODO: document behaviour of the invert flag


Settings
^^^^^^^^
* Range: The time unit the timer period refers to. One of ns, us, ms or s.
* Period: The period in units specified by Range.

  Minimum: 8 ns, maximum: 65535 s.

IRQ Units
~~~~~~~~~
Generates a logic pulse when one of the 7 VME IRQs triggers.

Soft Triggers
~~~~~~~~~~~~~
Software triggers which can be permanently activated or by executing the
folowing VME Script:

.. TODO: soft trigger script

Slave Triggers
~~~~~~~~~~~~~~
.. TODO: figure out slave triggers and the plan for multi-crate.
Not yet documented. Used for multi-crate setups.

Stack Busy
~~~~~~~~~~
The stack busy units are active while their corresponding VME command stack is
being executed.

In the mvme user interface the command stack numbers are augmented with the
event names defined in the VME config.

Sysclk
~~~~~~
This unit provides access to the 16 MHz VMEbus system clock.

Lookup Tables (Levels 1 and 2)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The MVLC contains a set of lookup tables useful for creating logic functions
and signal routing. Each lookup table (LUT) maps 6 input bits to 3 output bits.
This allows to implement 3 functions each mapping 6 input bits to one output
bit or a single 6 to 3 bit function.

The first three LUTs on level1 are hardwired to the NIM inputs. There is some
overlap as 14 NIM inputs are connected to the 3*6=18 inputs of the first three
LUTs.

.. TODO: better strobe explanation

The LUTs on level2 connect back to the level1 LUTs and each has 3 variable
inputs which can be connected to the level1 utility units or certain level1 LUT
outputs. Additionally the level2 LUTs each have a strobe input which is used to
synchronize the switching of the LUT outputs.

.. TODO: ui screenshot and explanation

.. TODO: example functions

StackStart
~~~~~~~~~~
These units start the execution of one of the 7 MVLC command stacks.

Settings
^^^^^^^^
* Number of the command stack to execute
* Activation flag

In the mvme user interface the command stack numbers are augmented with the
event names defined in the VME config.

MasterTrigger
~~~~~~~~~~~~~
Generates one of the master trigger signals for multi-crate setups.

Counters
~~~~~~~~
.. TODO: verify the input toggling behaviour
.. TODO: explain latch input
64 bit counter units incrementing by one each time the input toggles. Each
counter has an optional Latch Input.

The counter units can be read out via the MVCLs internal VME interface at base
address ``0xfff0000`` using the following VME script:

::
   setbase 0xffff0000

   # counter0
   0x0200 0x0308           # counter select
   0x030a 1                # latch the counter
   read a32 d16 0x0300     # counter readout
   read a32 d16 0x0302
   read a32 d16 0x0304
   read a32 d16 0x0306
   
   # counter1
   #0x0200 0x0309           # counter select
   #0x030a 1                # latch the counter
   #read a32 d16 0x0300     # counter readout
   #read a32 d16 0x0302
   #read a32 d16 0x0304
   #read a32 d16 0x0306
   
   # counter2
   #0x0200 0x030a           # counter select
   #0x030a 1                # latch the counter
   #read a32 d16 0x0300     # counter readout
   #read a32 d16 0x0302
   #read a32 d16 0x0304
   #read a32 d16 0x0306
   
   # counter3
   #0x0200 0x030b           # counter select
   #0x030a 1                # latch the counter
   #read a32 d16 0x0300     # counter readout
   #read a32 d16 0x0302
   #read a32 d16 0x0304
   #read a32 d16 0x0306

A special VME module called ``MVLC Timestamp/Counter`` is provided by mvme to
ease setting up a counter readout. Add an instance of this module to the VME
Event where you want to read out the counter, edit the readout script (under
'Readout Loop' in the user interface) and comment out all the counter blocks
except for the one that should be read out.

Reserved logic units and usage by mvme
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To implement events that should be periodcally read out mvme reserves the first
two timer and stack start units. Currently these units are not available for
modification in the user interface.

Whenever a periodic event is created the first available timer unit is setup
with the events readout period. The first available StackStart unit is then
connected to the timer and setup to start the events readout command stack.
