##################################################
Introduction
##################################################
**mvme** is a VME Data Acquisition solution by **mesytec** aimed at small scale nuclear physics
experiments involving a single VME controller. The goal of this project is to provide an easy to
setup, easy to use, cross-platform data acquisition system with basic data visualization and
analysis capabilities.

========
Features
========
* Easy creation and configuration of the VME setup

  * Multiple event triggers are possible (NIM, IRQ, periodic readout).
  * Multiple modules can be read out in response to a trigger.
  * VME Module setup using a config file style syntax
  * Templates for mesytec modules are provided.

* Live histogramming of readout data (1D and 2D)
* Flexible VME module data filtering
* Graphical analysis UI
* Writing raw data to disk (listfiles) with optional compression.
* Replays of listfile data.

===================
System Requirements
===================
* Any recent 64-bit Linux distribution or a 64-bit version of Windows XP or later.

  32-bit builds are possible but not recommended as the limited address space can be quickly used up
  when creating multiple histograms.
* WIENER VM-USB VME Controller
* libusb-0.1 (Linux) / libusb-win32 (Windows)

  The windows installer can optionally run a program to handle the driver installation (Zadig).

  It is also possible to compile builds using libusb-1.0 instead of the old libusb-0.1 API.

.. TODO Refer to the build instructions for 32-bit builds here.

* At least 4 GB RAM is recommended.


.. include:: installation.rstinc
.. include:: quickstart.rstinc
