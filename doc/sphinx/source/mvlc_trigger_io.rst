
.. highlight:: none

.. _mvlc-trigger-io:

MVLC Trigger and I/O setup
==================================================
What it is. How it is configured by mvme. What mvme reserves.


Introduction
------------
The MVLC VME controller contains a digitial logic module enabling flexible
configuration of readout trigger logic and timings. The module includes setup
of the front-panel NIM I/Os and ECL outputs, internal logic functions, signal
routing and access to utilities like timers, counters and the VME system clock.

The low-level setup of the Trigger I/O module is performed by writing to special
registers via MVLCs internal VME interface. This means standard VME commands
can be used to setup the module, create software triggers and read out counter
values.

mvme contains a dedicated graphical interface representing the structure and
internal connections of the logic module. This interface shields the user from
the low-level details, allows to setup custom signal names and simplifies
creating complex setups. The low-level VME commands used to setup the logic
module can still be viewed and manually edited if needed.

Structure and execution
-----------------------
.. TODO: screenshot of the gui

Logically the MVLC Trigger I/O module is divided into 4 levels with signals
originating on the lowest level and flowing through the system towards the
higher levels.

Each of the levels contains a set of specific units representing functionality
provided by the logic module. This includes the NIM I/Os, timers, internal
lookup tables, counters, access to command stacks etc.

The units from higher levels connect back to units from lower levels. Some of
these connections are hardwired while others can be dynamically selected from a
set of options.

Level 0 contains signal-generating units like NIM inputs, the VME system clock
and timers.

Levels 1 and 2 contain lookup tables which can be used to implement arbitrary
boolean logic and route signals to higher levels.

Level 3 consists of signal-consuming units like NIM outputs, counters and
units for executing command stacks.

.. TODO: add information about internal clocking and processing (maybe)
.. TODO: figure out internal timings and add more about how stuff works. maybe
.. TODO: also mention strobes and latches.

The system interally runs at a fixed clock rate. Signal state changes thus
happen at discrete time intervals.

User Interface and integration into mvme
----------------------------------------

Inside mvme the MVLC Trigger I/O module will appear as the topmost item in the
VME Config tree when one of the MVLC variants (USB or Ethernet) is selected as
the VME Controller.

.. TODO: screenshot highlighting the trigger io module

Double-clicking this module will open a dedicated GUI representing the current
configuration of the logic module. The view can be zoomed via the mouse wheel
and panned by holding down the left mouse button and moving the cursor.

Each of the blocks can be double-clicked to open an editor window specific to
the unit or groups of units. Input and output pins are represented by small
circles near the edges of each block. Connection between pins are drawn as
arrows from source to destination.

To keep the user interface from being cluttered by lots of connection arrows
only *active* connections are drawn. NIM and ECL units have their own
activation flags present in the hardware while other units such as timers -
which interally are always active - have a software-only activation flag.
Lookup table inputs are only considered active if the corresponding input is
actually used in the logic function implemented by the LUT.

Each of the specific editor windows allows editing the names of input/output
pins which makes routing and connecting signals much easier.

By default changes made in any of the editors are applied immediately upon
closing the editor or pressing the ``Apply`` button as long as mvme is
connected to an MVLC. This means the software representation of the Trigger I/O
module is converted to a list of VME write commands targeting the MVLC and then
this list is directly executed. Use the 'Autorun' button from the top toolbar
to toggle this behaviour.

If you want to view or edit the VME write commands directly use the ``View
Script`` button, make your changes inside the text editor that opens up and
then pres the ``Reparse from script`` button to update the user interface.

.. TODO: ref to units section here.
Detailed descriptions of the available units and their corresponding GUI
editors can be found in the next section (I/O and logic units).

When starting a new DAQ run the initialization procedure will apply the current
logic setup to the MVLC before further initializing any modules and setting up
the readout stacks.

Reserved logic units and usage by mvme
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
To implement events that should be periodcally read out mvme reserves the first
two timer and stack start units. Currently these units are not available for
modification in the user interface.

Whenever a periodic event is created the first available timer unit is setup
with the events readout period. The first available StackStart unit is then
connected to the timer and setup to start the events readout command stack.

Note that if more than two periodic VME events are created, the rest of the
Timer and StackStart units will also be used by mvme. Having more than 4
periodic events defined in the VME config is not allowed and will lead to an
error at startup.

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
Software triggers which can be permanently activated via the GUI or by
executing one of the folowing VME Scripts:

::
   setbase 0xffff0000		# use the mvlc vme interface as the base address
   0x0200 0x0006          	# select soft_trigger0 (Level0.Unit6)
   0x0300 1                	# activate the trigger

:: 
   setbase 0xffff0000		# use the mvlc vme interface as the base address
   0x0200 0x0007          	# select soft_trigger1 (Level0.Unit7)
   0x0300 1                	# activate the trigger

To use the above scripts in mvme right-click the ``Manual`` section in the VME
Config area and choose ``Add Script``, type a name and double-click the newly
created script to edit it. Then paste the script text into the editor and use
the ``Run Script`` button to execute the script.

Slave Triggers
~~~~~~~~~~~~~~
Activates when one of the slave triggers fires. This feature will be available
in the future with a special multi-crate firmware and supporting software.

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
Generates a master trigger in multi-crate setups. This feature will be
available in the future with a special multi-crate firmware and supporting
software.

Counters
~~~~~~~~
.. TODO: explain latch input
64 bit counter units incrementing by one each time the input rises. Each
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

Examples
--------

* NIM input to stack start/counter + counter readout
* Timer/sysclk to counter + counter readout
* Timer to stackstart for periodic events
* SoftTrigger to NIM output
* Some LUT setups
