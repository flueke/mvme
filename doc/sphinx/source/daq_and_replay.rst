==================================================
DAQ / Replay controls
==================================================

.. autofigure:: images/intro_daq_control.png

    DAQ controls


The effects of the buttons depend on the current mode - DAQ or replay - and the
current state of the system:

.. table:: DAQ control actions
    :name: table-daq-control-actions

    +--------------+-----------------------------------+----------------------------------------+
    | Action       | DAQ mode                          | Replay mode                            |
    +==============+===================================+========================================+
    | Start        | * Run :ref:`vme-config-daq-start` | Start replay from beginning of file    |
    |              | * Open new listfile               |                                        |
    +--------------+-----------------------------------+----------------------------------------+
    | Stop         | * Run :ref:`vme-config-daq-stop`  | Stop and rewind to beginning of file   |
    |              | * Close listfile                  |                                        |
    +--------------+-----------------------------------+----------------------------------------+
    | Pause        | * Leave DAQ mode                  | Pause replay                           |
    |              | * No special procedures are run   |                                        |
    +--------------+-----------------------------------+----------------------------------------+
    | 1 Cycle      | * Start DAQ for one cycle         | * Replay the next event from the file  |
    | / Next Event | * Dump received data to log view  | * Dump event data to log view          |
    +--------------+-----------------------------------+----------------------------------------+

The *Start* and *1 Cycle / Next Event* buttons allow to choose what should
happen with existing histogram data. Selecting *Clear* will clear all
histograms in the current analysis before accumulating new data. Using *Keep*
allows to accumulate the data from multiple replays or DAQ runs into the same
histograms.

The *Reconnect* button will attempt to reconnect to the current VME controller.
This is needed when using the VM-USB controller and power-cycling the VME crate
as USB disconnects are currently not detected.

If *Write Listfile* is checked a new output listfile will be created when
starting a DAQ run. The file will be created in the *listfiles* subdirectory of
the current workspace. The filename is based off the current timestamp to make
it unique.

If *Write to ZIP* is checked a ZIP archive with optional compression will be
created instead of a flat *.mvmelst* file.

Replaying from listfile
-------------------------

To replay data from a listfile use *File -> Open listfile* and choose a *.zip*
or *.mvmelst* file.

When opening a listfile the VME config included inside the file is loaded and
will replace the current config. The global mode will be switched to
*Listfile*. To go back to DAQ mode use *File -> Close Listfile*.
