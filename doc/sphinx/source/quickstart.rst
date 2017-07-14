##################################################
Quickstart Guide
##################################################

The quickstart guide explains how to create a simple setup using the WIENER
VM-USB VME controller and one mesytec VME module. The modules internal pulser
is used to generate test data. Data readout is triggered by the module using
IRQ1.

.. TODO: Add a second, periodic event to read out the event counter

.. note::
  In this example an MDPP-16 with the SCP firmware is used but any **mesytec**
  VME module should work. For other modules the value written to the pulser
  register (0x6070) might need to be adjusted. Refer to the modules manual for
  details.

* Start mvme and create a new workspace directory using the file dialog that
  should open up. This directory will hold all configuration files, recorded
  listfiles, exported plots, etc.

* Three windows will open:

  * A main window containing DAQ controls, the VME configuration tree and a
    statistics area.

  * The analysis window. As there are no VME events and modules defined yet the
    window will be empty.

  * A log view where runtime messages will appear.

.. autofigure:: images/quickstart_gui_overview.png
    :scale-latex: 75%

    GUI overview

If a VM-USB VME controller is connected to the PC and powered on mvme should
automatically find and use it. *VME Controller* in the DAQ Control area should
show up as *Connected*. Also the VM-USB firmware version will be printed to the
Log View.

==================================================
VME Setup
==================================================
* Select the mvme main window containing the *VME Config* area.

* Create a VME event:

  * Right-click the *Events* entry in the VME tree and select *Add Event*.

  * Select *Interrupt* in the *Condition* combobox. Keep the defaults of *IRQ
    Level = 1* and *IRQ Vector = 0*.

.. autofigure:: images/quickstart_event_config.png
   :scale-latex: 60%

   Event Config Dialog

* Create a VME module:

  * Right-click the newly created event (called "event0" by default) and select
    *Add Module*.

  * Select *MDPP-16_SCP* from the module type list. If you changed the modules
    address encoders adjust the *Address* value accordingly (the address
    encoders modify the 4 most significant hex digits).

.. autofigure:: images/quickstart_module_config.png
   :scale-latex: 60%

   Module Config Dialog

The VME GUI should now look like shown in :ref:`quickstart-vme-tree01`.

.. _quickstart-vme-tree01:

.. autofigure:: images/intro_vme_tree01.png
   :scale-latex: 60%

   VME Config Tree

* Double-click the *Module Init* entry to open a VME Script Editor window.
  Scroll to the bottom of the editor window and adjust the register value for
  the modules internal pulser:

  ``0x6070 3``

  This line tells mvme to write the value ``3`` to register address ``0x6070``.
  The address is relative to the module base address.

* Click the *Apply* button on the editors toolbar to commit your changes to the
  VME configuration. Close the editor window.

* Start editing the *VME Interface Settings* VME Script. Near the top of the
  script set the *irq level* to 1:

  ``0x6010 1``

  This makes the module send IRQ 1 if it has data to be read in its internal
  buffer. The other parameters can be left at their default values. Click
  *Apply* and close the editor window.

.. autofigure:: images/quickstart_edit_vme_interface_settings.png
   :scale-latex: 60%

   VME Interface Settings with IRQ Level set to 1

==================================================
Analysis Setup
==================================================
* Activate the *Analysis UI* window (the shortcut is ``Ctrl+2``). The event
  containing the module just created should be visible in the UI.

* Right-click the module and select *Generate default filters*. Choose *Yes* in
  the messagebox that pops up. This will generate a set of data extraction
  filters, calibration operators and histograms for the module.

.. _quickstart-analysis-default-filters:

.. figure:: images/intro_analysis_default_filters.png
   :width: 8cm

   Analysis UI with MDPP-16 default objects


==================================================
Starting the DAQ
==================================================
Activate the main window again (``Ctrl+1``). Make sure the *VME Controller* is
shown as *Connected* in the top part of the window. Optionally uncheck the box
titled *Write Listfile* to avoid writing the test data to disk.

.. _quickstart-daq-control:

.. figure:: images/intro_daq_control.png
   :width: 8cm

   DAQ control

Press the *Start* button to start the DAQ. Check the *Log View* (``Ctrl+3``)
for warnings and errors.

In the *Analysis UI* double-click the histogram entry called *amplitude_raw*
(bottom-left corner in the *L0 Data Display* tree) to open a histogram window.

If data acquisition and data extraction are working properly you should see new
data appear in the histogram. Use the spinbox at the top right to cycle through
the individual channels.

.. _quickstart-amplitude-histogram:

.. figure:: images/intro_amplitude_histogram.png
   :width: 12cm

   Amplitude histogram

You can pause and/or stop the DAQ at any time using the corresponding buttons
at the top of the main window.

.. ==================================================
.. Troubleshooting
.. ==================================================
.. .. warning::
..     TODO: Refer to a global troubleshooting section

.. vim:ft=rst
