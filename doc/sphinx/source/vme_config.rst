.. highlight:: none

.. _vme-config-reference:

==================================================
VME configuration
==================================================

Structure
---------

The VME configuration in mvme models the logical VME setup. *Modules* that
should be read out as a result of the same trigger condition are grouped
together in an *Event*: ::

    Event0
        Module0.0
        Module0.1
        ...

    Event1
        Module1.0
        Module1.1
        ...

The type of available trigger conditions depends on the VME controller in use.
With the WIENER VM-USB the following triggers are available:

* NIM

  One external NIM input

* IRQ1-7

  The standard VME interrupts

* Periodic

  VM-USB supports one periodic trigger which is executed every ``n * 0.5s``
  (*Period*) or on every ``m-th`` data event (*Frequency*). If both values are
  set both internal counters are reset on each activation of the trigger. Refer
  to section 3.4.3 of the VM-USB manual for details.


Module and Event configuration
------------------------------

The module and event configuration is done using :ref:`VME scripts
<vme-script-reference>` which contain the commands necessary to initialize and
readout each module.

At the module level the following phases are defined:

* Reset

  Reset the module to a clean default state.

* Init

  Setup the module by writing specific registers.

* Readout

  The code needed to readout the module whenever the trigger condition fires.

The event level distinguishes between the following phases:

* Readout Cycle Start / End

  Inserted before / after the readout commands of the modules belonging to this
  event. The *Cycle Start* script is currently empty by default, the *Cycle
  End* script notifies the modules that readout has been performed. By default
  this is done by writing to the multicast address used for the event.

* DAQ Start / Stop

  Executed at the beginning / end of the a DAQ run. The purpose of the *DAQ
  Start* script is to reset module counters and tell each module to start data
  acquisition. *DAQ End* is used to tell the modules stop data acquisition. By
  default both scripts again use the multicast address of the corresponding
  event.

DAQ startup procedure
---------------------

* Reset and setup the VME controller
* Assemble readout code from configured Events

  For each Event do:

  * Add *Cycle Start* script
  * For each Module:

    * Add Module readout script
    * Add "Write EndMarker" command

  * Add *Cycle End* script

* Upload the readout code to the controller and activate triggers
* Execute global *DAQ Start* scripts
* Initialize Modules

  For each Event do:

    * For each Module do:

      * Run *Module Reset*
      * Run all *Module Init* scripts

    * Run the Events *Multicast DAQ Start* script
* Set the controller to autonomous DAQ mode

Control is handed to the VME controller. mvme is now reading and
interpreting data returned from the controller.

DAQ stop procedure
------------------

* Tell the VME controller to leave autonomous DAQ mode
* Read leftover data from the VME controller
* Run the *DAQ Stop* script for each Event
* Execute global *DAQ Stop* scripts

DAQ controls
------------

.. autofigure:: images/intro_daq_control.png

    DAQ controls
