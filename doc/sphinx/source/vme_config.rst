.. _vme-config-reference:

==================================================
VME Setup
==================================================

.. warning:: Write me!

Structure
---------

Event
    Module
    Module
    ...

Event
    Module
    Module
    ...

...

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

* Run the *Multicast DAQ Start* script for each Event
* Make the controller enter autonomous DAQ mode

  Control is handed to the VME controller. mvme is now reading and
  interpreting data returned from the controller.

DAQ stop procedure
------------------

* Tell the controller to leave autonomous DAQ mode
* Read leftover data from the VME controller
* Run the *DAQ Stop* script for each Event
* Execute global *DAQ Stop* scripts
