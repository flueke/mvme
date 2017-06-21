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
* WIENER VM-USB VME Controller
* libusb-0.1 (Linux) / libusb-win32 (Windows)

  The windows installer can optionally run a program to handle the driver installation (Zadig).

  It is also possible to compile builds using libusb-1.0 instead of the old libusb-0.1 API.

.. warning::
    TODO: Refer to the build instructions


* At least 4 GB RAM is recommended.




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
