.. index:: Changelog, Changes

##################################################
Changelog
##################################################

Version 1.4.9-rc
----------------
* New feature: listfile splitting (MVLC only!)

  When recording readout data the output listfile can now be split either based
  on file size or elapsed time. Each partial listfile ZIP archive is in itself
  a complete, valid mvme listfile and includes the VME config, analysis config
  and logged messages.

  Replaying from split listfiles currently has to be done manually for each
  part. Using the 'keep histo contents' in mvme allows to accumulate data from
  multiple (partial) listfiles into the same analysis.

* New feature: VME modules can now be saved to and loaded from JSON files. This
  can be used to create custom VME modules without having to use the mvme VME
  template system.

* DAQ run number is now increment on MVLC readout stop to represent the *next*
  run number.

* Show the original incoming data rate in the analysis window when replaying
  from listfile.

* Improve Triva7 VME module templates.

* VME Config: allow moving modules between VME Events via drag&drop.

* Fix 'VME Script -> Run' in the MVLC Debug GUI

Version 1.4.8.1
---------------
Make mvme build against qwt version older than 6.2.0 again.

Version 1.4.8
-------------

* [mvlc]

  - Simplify the readout parser: modules readout data may now consist of either
    a dynamic or a fixed part instead of prefix, dynamic and suffix parts. This
    allows for a simpler callback interface for the parser.

    The previous, more complex structure can be recrated by adding multiple
    modules to the VME config, each performing either fixed size reads or a
    block transfer.

  - Add support for new features in firmware FW0021:

    * New vme_script commands to work with the MVLC stack accumulator.

      See :ref:`vme_command-mvlc_signal_accu` and the commands following it.

    * Add ability to define custom and inline MVLC stacks in VME scripts.

      See :ref:`vme_command-mvlc_stack_begin` and :ref:`vme_command-mvlc_custom_begin`.

    * The readout parser now knows about the accumulator and emulated
      accumulator block reads.

    * Support CR/CSR addressing modes.

* [analysis]

  - Improvements to the EventBuilder module. This version does work with
    non-mesytec modules being present in an event and allows to exclude modules
    from the timestamp matching algorithm.

  - Improve Histo1D 'Print Stats' output

  - Crash fix when loading a session file with unconnected histograms.


* [vme_templates]

  Add module templates for the GSI Triva 7 trigger module.

* [build]

  - Upgrade to Qt 5.15.2 and Qwt 6.2.0


Version 1.4.7
-------------

* Reopen to the last used VME config when closing a listfile.

* When saving VME/analysis config files suggest a filename based on the
  workspace directory.

* Add a ``--offline`` option to mvme which disables any connection attempts to
  the VME controller. Useful for replay-only sessions.

* Improve MVLC stack error reporting.

* Decrease number of readout buffers in-flight to reduce latency when stopping
  a run/replay.

* Various bug and crash fixes.

* [analysis]

  - Add an EventBuilder module to the analysis processing chain.

  - Fix analysis stats display when using more than 12 modules in an event.

  - Prepend the module name to analysis objects generated when adding the default filters.

* [vme_script]

  - Add support for MVLC stacks containing custom data (mvlc_custom_begin).

  - Add support for new MVLC commands in Firmware 0x0020.

* [packaging]

  - make installed files and directories group and world readable.
  - re-add the mvme.sh startup shell script to the bin/ directory.


Version 1.4.6
-------------
* [mvlc]

  - Improve immediate MVLC/VME command latency when using the DSO.
  - Trigger/IO updates

* [analysis]

  - Fix crash in the ExportSink ("File Export") operator.
  - Add CSV output option to the ExportSink.

* [vme] Change default vme amods from the privileged to the user variants.


Version 1.4.5
-------------
* Create an empty analysis when opening a workspace and no existing analysis
  could be loaded from the workspace. This fixes an issue where analysis
  objects from the previously opened workspace still existed after changing the
  workspace.

Version 1.4.4
-------------
* [vme_script] Behavior changes:

  - Do not accept octal values anymore. '010' was parsed as 8 decimal while
    '080' - which is an invalid octal literal - was parsed as a floating point
    value and interepreted as 8 decimal.

  - Floating point parsing is now only applied if the literal contains a '.'.

* [analysis] Module hit counts in the top left tree now display the count and
  rate of non-empty readout data from the module. Previously they showed all
  hits and where thus equal to parent event rate unless multi-event splitting
  was in effect.

* [vmusb] Fix readout being broken.

* Do not auto create non-existing workspace directories on startup. Instead ask
  the user to open an existing workspace or create a new one.

* Do not set default vme and analysis config file names when creating a
  workspace or no previously loaded files exist in the current workspace. This
  makes the user have to pick a name when saving each of the files and should
  make it less likely to accidentially overwrite existing configs.

Version 1.4.3
-------------
* [mvlc] Add support for the oscilloscope built into the MVLC since firmware FW0018.

* [analysis]

  - Remove the vme module assignment dialog. Instead show data sources
    belonging to unassigned modules in a hierarchy in the top left tree of the
    analysis window. Data sources can be dragged from there onto known modules
    to assign them.

  - Add static variables to the Expression Operator. These variables exist per
    operator instance and persist their values throughout a DAQ or replay run.

  - Add a ScalerOverflow operator which outputs a contiguous increasing value
    given an input value that overflows. This can be used to handle data like
    module timestamps which wrap after a certain time.

  - The RateMonitor can now display a plain value on the x axis instead of time
    values. Useful when plotting timestamp or counter values.

  - Added division to the binary equation operator.

* Better handling of vme/analysis config files when opening listfiles to reduce
  the number of instances where the vme and analysis configs diverge.

* Add print statements to the module reset vme template scripts.

Version 1.4.2
-------------

* [vme_templates]

  - Wait 500ms instead of 50ms in the reset scripts of MDPP-32_PADC/QDC

  - Update MDPP-32_QDC calibration to 16 bits

  - Do not set vme mcst address in the mvlc_timestamper ``VME Interface Settings`` script.

* [analysis]

  - Improve Rate Monitor draw performance

  - Make Rate Estimation work in projections of 2D histograms

  - Analysis session data parsing fixes

Version 1.4.1
-------------
* [vme_templates] Fix gain calculation in MDPP16-SCP ``Frontend Settings`` script.

Version 1.4.0
-------------
* [mvlc] Trigger/IO updates for firmware FW0017

  - Replace IRQ, SoftTrigger and SlaveTrigger units with the new
    TriggerResource units

  - Support the IRQ input, L1.LUT5/6 and L2.LUT2 units

  - Support Frequency Counter Mode for Counter units

  - Basic support for the Digital Storage Oscilloscpe built into the Trigger/IO
    system.

  - Crash fixes when parsing Trigger/IO scripts

* [mvlc] Updates to the DAQ Start and Stop sequence

* [vme_config] The order of Modules within an Event can now be changed via drag
  and drop.

* [analysis]

  - Performance and visual updates for the RateMonitors

  - Display directory hierarchy in Histogram and RateMonitor window titles

* [vme_templates]

  - Add the new MDPP-16/32 channel based IRQ signalling.

  - Add the 'stop acq' sequence to all module 'VME Interface Settings' scripts.
    This makes modules not produce data/triggers directly after being
    intialized but only after the 'Event DAQ Start' script has been executed.

Version 1.3.0
-------------
* [mvlc] Support MVLC ethernet readout throttling

  - Throttling is done by sending 'delay' commands to the MVLC which then adds
    small gaps between outgoing ethernet packets thus effectively limiting the
    data rate.

  - The MVLC will block the VME readout side if it cannot send out enough
    ethernet packets either due to reaching the maximum bandwidth or due to
    throttling. This behaves in the same way as USB readouts when the software
    side cannot keep up with the USB data rate.

  - The delay value is currently calculated based on the usage level of the
    readout socket receive buffer. Throttling starts at 50% buffer usage level
    and increases exponentially from there.

  This method of ethernet throttling is effective when the receiving PC cannot
  handle the incoming data rate, e.g. because it cannot compress the listfile
  fast enough. Instead of bursts of packet loss which can lead to losing big
  chunks of readout data the readout itself is slowed down, effectively
  limiting the trigger rate. The implementation does not compensate for packet
  loss caused by network switches or other network equipment.

  Throttling and socket buffer statistics are shown at the bottom of the main
  window, below the VME config tree.

* [mvlc] readout_parser fixes:
  - disabled VME modules where confusing the readout parser
  - stale data from the previous DAQ run was remaining in the buffers

* [mvlc] Updates and fixes for the trigger IO editor.

* [mvlc] When creating a new VME config a new default trigger IO setup is
  loaded. The setup provides 5 trigger inputs, 5 gated trigger outputs, a free
  trigger output and daq_start, stack_busy and readout_busy signals on the
  NIMs. The setup is intended to be used with two events: one for the readout
  and one periodic event for counter readout.

* [analysis] Allow directories, copy/paste and drag/drop for raw histograms
  (bottom-left tree view). When generating default filters and histograms for a
  module the histograms are also placed in a directory instead of being
  attached to special module nodes. When loading analysis files from previous
  versions the missing directories are automatically created.

* [analysis] Updated the multievent_splitter to work with modules which do not
  contain the length of the following event data in their header word. Instead
  the event length is determined by repeatedly trying the module header filter
  until it matches the next header or the end of the readout data is reached.

* [analysis] Updates and fixes for the RateMonitors

* [vme_templates]

  - Updates to the mesytec VMMR template.

  - Updates to the CAEN v785 template.

  - Add templates for the  CAEN V1190A Multihit TDC.

* [vme_script] add 'readabs' command

* [core] Improve the high level stopDAQ logic and resulting state updates. This in turn
  makes stopping the DAQ via JSON-RPC work reliably.

Version 1.2.1
-------------
* [analyis] Fix two crashes when using the ExportSink

Version 1.2.0
-------------
* [mvlc] Update mesytec-mvlc lib to work around an issue were MVLC_ETH was not
  able to connect under Windows 10 Build 2004.

  This issue has also been fixed in MVLC Firmware FW0008.

* [vme_templates] Add VME and analysis templates for the mesytec MDPP-16_CSI,
  MDPP-16_PADC and MDPP-32_PADC module variants.

* [vme_templates] Add templates for the MDI-2 starting from firmware FW0300.

* [vme_templates] Add files for the CAEN V830 latching scaler.

* [vme_script] Add a new 'mblts' (swapped block read) command for the MVLC
  which swaps the two 32-bit words received from MBLT64 block reads.

  This was added to the MVLC to support the CAEN V830 and possibly other
  modules which have the data words swapped compared to the mesytec modules.

* [analysis] Generate histograms and calibrations for ListfilterExtractors
  found in module analysis template files. This was added for the V830 which is
  the first template file to use ListfilterExtractors.

* [core] Add facilities for storing the log messages generated by mvme to disk:

  - All messages generated during DAQ runs (from 'DAQ start' to 'DAQ stop') are
    written to a file in the workspace 'run_logs/' directory.

    The maximum number of files kept is limited to 50. On exceeding the limit
    the oldest file is removed. Filenames are based on the current date and
    time.

    This feature was added because previously only the logs from *successful*
    DAQ starts where kept on disk (inside the listfile ZIP archive
    generated by mvme). Log contents from aborted starts had to be manually
    copied from the log window.

  - All messages generated by mvme are written to 'logs/mvme.log'. On opening a
    workspace an existing logfile is moved to 'logs/last_mvme.log' and a new
    logfile is created.

    These files contain all messages generated by mvme, even those produced
    while no DAQ run was active.

* [event_server] Use relative path for dlopen() in mvme_root_client. Attempts
  to fix an issue where the analysis.so could not be loaded on some machines.

Version 1.1.0
-------------
* MVLC support is now implemented using the mesytec-mvlc library.  Listfiles
  created by this version of mvme can be replayed using the library (e.g. the
  mini-daq-replay program).

Version 1.0.1
-------------
* [vme_templates] Add new VMMR_Monitor module intended for reading out MMR
  monitor data (power, temperature, errors).

* [vme_templates] Module templates can now specify a set of default variables
  to create when the module is instantiated.

* [vme_templates] Allow using ListFilterExtractors in module analysis templates
  in addition to MultiWordDataFilters.

* [mvlc] Update trigger io editor connection bars to reflect changes to the firmware.

* [mvlc] Fix potential data loss under very high data rates.

* [doc] Updates to the Installation section.

Version 1.0.0
-------------
* Add ability to run the data acquisition for a limited amount of time before
  automatically stopping the run.

* Add VME templates for the MDPP-32 (SCP and QDC variants).

* [vme_script] Drop support for the 'counted block read` commands. They are
  complex, rarely used and the MVLC does not currently support them. As long as
  a VME module supports either reading until BERR or can be read out using a
  fixed amount of (M)BLT cycles there is no need for these special commands.

* [vme_script] VME scripts now support floating point values, variables and
  embedded mathematical expressions.

* [vme_config] Updates to the mesytec module templates and the internal config
  logic to make use of the new VME script variables.

  These changes make IRQ and MCST handling with multiple modules and events
  much simpler. When using only mesytec modules no manual editing of scripts is
  required anymore.

  When loading a config file from a previous mvme version all module and event
  scripts will be updated to make use of the standard set of variables added to
  each VME event.

* Improve UI responsiveness with the MVLC at low data rates.

* Multiple MVLC fixes and improvements.

* Various bugfixes and UI improvements

  - VME Script error messages are now highlighted in red in the log view.

  - Speed up creating and updating the analysis tree views. This is especially
    noticeable when using many modules or many VME events.


* Upgrade Qt to version 5.14.1 on the build servers.

* Do not ship libstdc++ with the linux binary package anymore. It caused issues
  in combination with setting LD_LIBRARY_PATH as is done in the initMVME shell
  script.

Version 0.9.6
-------------
* Improved support for the MVLC. Among others VME Scripts can now be directly
  executed during a DAQ run without having to pause and resume the DAQ.

* New UI for setting up the MVLC Trigger and I/O logic system.

* Updates to the auto-matching of vme and analysis objects on config load.

* Improved the mvlc_root_client

* Documentation updates

* Improved VME module templates

* Various stability and bugfixes

Version 0.9.5.5
---------------
* This is the first version with support for the upcoming mesytec MVLC VME
  controller.

* Added the EventServer component which allows to transmit extracted readout
  data over a TCP connection.

* Added a client for the EventServer protocol which generates and loads ROOT
  classes, fills instances of the generated classes with incoming readout data
  and writes these objects out to a ROOT file. Additionally user defined
  callbacks are invoked to perform further analysis on the data.

Version 0.9.5.4
---------------
* Log values written to the VMUSB ActionRegister when starting / stopping the
  DAQ

Version 0.9.5.3
---------------
* Allow access to all VMUSB registers via vme_script commands
  ``vmusb_write_reg`` and ``vmusb_read_reg``

* Fix a crash in Histo1DWidget when resolution reduction factor was set to 0

Version 0.9.5.2
---------------
* Fix a race condition at DAQ/replay startup time

* Remove old config autosave files after successfully loading a different
  config. This fixes an issue where apparently wrong autosave contents where
  restored.

* Rewrite the analysis session system to not depend on HDF5 anymore. This was
  done to avoid potential issues related to HDF5 and multithreading.

.. note::
  Session files created by previous versions cannot be loaded anymore. They
  have to be recreated by replaying from the original readout data.

Version 0.9.5.1
---------------

This release fixes issues with the code generated by the analysis export
operator.

Specifically the generated CMakeLists.txt file was not able to find the ROOT
package under Ubuntu-14.04  using the recommended way (probably other versions
and other debian-based distributions where affected aswell). A workaround has
been implemented.

Also c++11 support is now properly enabled when using CMake versions older than
3.0.0.

Version 0.9.5
-------------

.. note::
  Analysis files created by this version can not be opened by prior versions
  because the file format has changed.

This version contains major enhancements to the analysis user interface and
handling of analysis objects.

* It is now possible to export an object selection to a library file and import
  objects from library files.

* Directory objects have been added which, in addition to the previously
  existing userlevels, allow to further structure an analysis.

  Directories can contain operators, data sinks (histograms, rate monitors,
  etc.) and  other directories.

* Objects can now be moved between userlevels and directories using drag and
  drop.

* A copy/paste mechanism has been implemented which allows creating a copy of a
  selection of objects.

  If internally connected objects are copied and then pasted the connections
  will be restored on the copies.

Other fixes and changes:

* New feature: dynamic resolution reduction for 1D and 2D histograms.

  Axis display resolutions can now be adjusted via sliders in the user
  interface without having to change the physical resolution of the underlying
  histogram.

* Improved hostname lookups for the SIS3153 VME controller under Windows. The
  result is now up-to-date without requiring a restart of mvme.

* Add libpng to the linux binary package. This fixes a shared library version
  conflict under Ubuntu 18.04.

* SIS3153: OUT2 is now active during execution of the main readout stack.
  Unchanged: OUT1 is active while in autonomous DAQ mode.

* The Rate Monitor can now take multiple inputs, each of which can be an array
  or a single parameter.

  Also implemented a combined view showing all rates of a Rate Monitor in a
  single plot.

* Add new VM-USB specific vme script commands: ``vmusb_write_reg`` and
  ``vmusb_read_reg`` which allow setting up the VM-USB NIM outputs, the
  internal scalers and delay and gate generators.

  Refer to the VM-USB manual for details about these registers.

Version 0.9.4.1
---------------

* Fix expression operator GUI not properly loading indexed parameter
  connections

* Split Histo1D info box into global and gauss specific statistics. Fixes to
  gauss related calculations.

Version 0.9.4
-------------
* New: :ref:`Analysis Expression Operator<analysis-ExpressionOperator>`

  This is an operator that allows user-defined scripts to be executed for each readout
  event. Internally `exprtk`_ is used to compile and evaluate expressions.

* New: :ref:`Analysis Export Sink<analysis-ExportSink>`

  Allows exporting of analysis parameter arrays to binary files. Full and sparse data
  export formats and optional zlib compression are available.

  Source code showing how to read and process the exported data and generate ROOT
  histograms can be generated.

* New: :ref:`Analysis Rate Monitor<analysis-RateMonitorSink>`

  Allows to monitor and plot analysis data flow rates and rates calculated from successive
  counter values (e.g. timestamp differences).

* Moved the MultiEvent Processing option and the MultiEvent Module Header Filters from the
  VME side to the analysis side. This is more logical and allows changing the option when
  doing a replay.

* General fixes and improvements to the SIS3153 readout code.

* New: JSON-RPC interface using TCP as the transport mechanism.

  Allows to start/stop DAQ runs and to request status information.


Version 0.9.3
-------------

* Support for the Struck SIS3153 VME Controller using an ethernet connection
* Analysis:

  * Performance improvments
  * Better statistics
  * Can now single step through events to ease debugging
  * Add additional analysis aggregate operations: min, max, mean, sigma in x
    and y
  * Save/load of complete analysis sessions: Histogram contents are saved to
    disk and can be loaded at a later time. No new replay of the data is
    neccessary.
  * New: rate monitoring using rates generated from readout data or flow rates
    through the analysis.

* Improved mesytec vme module templates. Also added templates for the new VMMR
  module.
* More options on how the output listfile names are generated.
* Various bugfixes and improvements

Version 0.9.2
-------------

* New experimental feature: multi event readout support to achieve higher data
  rates.
* DataFilter (Extractor) behaviour change: Extraction masks do not need to be
  consecutive anymore. Instead a "bit gather" step is performed to group the
  extracted bits together and the end of the filter step.
* UI: Keep/Clear histo data on new run is now settable via radio buttons.
* VMUSB: Activate output NIM O2 while DAQ mode is active. Use the top yellow
  LED to signal "USB InFIFO Full".
* Analysis performance improvements.
* Major updates to the VME templates for mesytec modules.

Version 0.9.1
-------------

* Record a timetick every second. Timeticks are stored as sections in the
  listfile and are passed to the analyis during DAQ and replay.
* Add option to keep histo data across runs/replays
* Fixes to histograms with axis unit values >= 2^31
* Always use ZIP format for listfiles

.. _exprtk: http://www.partow.net/programming/exprtk/index.html
