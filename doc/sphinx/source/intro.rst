==================================================
Introduction
==================================================
**mvme** is a VME Data Acquisition solution by **mesytec** aimed at small scale nuclear physics
experiments involving a single VME controller. The goal of this project is to provide an easy to
setup, easy to use, cross-platform data acquisition system with basic data visualization and
analysis capabilities.

Features
--------
* Easy creation and configuration of the VME setup

  * Multiple event triggers are possible (NIM, IRQ, periodic readout).
  * Multiple modules can be read out in response to a trigger.
  * VME Module setup using a config file style syntax
  * Templates for **mesytec** modules are provided.

* Live histogramming of readout data (1D and 2D)
* Flexible VME module data filtering
* Graphical analysis UI
* Writing raw data to disk (listfiles) with optional compression.
* Replays of raw data.

System Requirements
-------------------
* Any recent 64 bit Linux distribution or a 64 bit version of Windows XP or later.

  32 bit builds are possible but not recommended as the limited address space can be quickly used up
  when creating multiple histograms.
* WIENER VMUSB VME Controller
* libusb-0.1 (Linux) / libusb-win32 (Windows)

  The windows installer can optionally run a program to handle the driver installation (Zadig).
* At least 4 GB RAM is recommended.

Installation
------------
.. warning::
    TODO: Write this

Quickstart
----------
.. warning::
    INCOMPLETE

The quickstart guide explains how to create a simple setup using the VMUSB VME
controller and one mesytec VME module. Data acquisition is triggered by the
module using IRQ1. The internal pulser is used to generate test data.
Additionally the modules event counter registers are read out periodically
using a different trigger.

* Start **mvme** and create a new workspace directory using the file dialog
  that should open up. This directory will hold all configuration files,
  recorded listfiles, exported plots, etc.

* Three windows will open:
  * A main window containing DAQ and listfile controls, the VME configuration
    tree and a DAQ statistics area.
  * The analysis window. As there are no VME events and modules defined yet the
    window will be empty.
  * A log view where runtime messages will appear.

* Create a VME event:
  * Right click the *Events* entry in the VME tree and select *Add Event*.
  * Select *Interrupt* in the *Condition* combobox. Keep the defaults of *IRQ
    Level = 1* and *IRQ Vector = 0*.

* Create a VME module:
  * Right click the newly created event (called "event0" by default) and select
    *Add Module*.
  * Select your module type and optionally give the module a name. If the
    modules address encoders are set to anything other than ``0000`` adjust the
    *Address* value accordingly.

.. ==================================================
.. Quickstart
.. ==================================================
.. .. FIXME: Incomplete and not great
.. * Create event, select irq 1, vector 0.
.. * Create module, edit module interface settings. Change irq to 1.
.. * Edit module settings, enable pulser for testing
.. * In the analysis window right click the module and select 'generate default filters'
.. * In the main window press start to enter DAQ mode.
.. * Check the log for any errors that might have occured during initialization
.. * Double click the amplitude histograms to verify the pulser is working and
..   data is being received properly.
