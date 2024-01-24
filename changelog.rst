.. index:: Changelog, Changes

##################################################
Changelog
##################################################

Version 1.11-rc
---------------

* Support for new MVLC firmware FW0037 features:

  - The 4 new StackTimer units can now be used to create periodic readout events
    without having to use ``TriggerResource`` and ``StackStart`` units.

  - New event trigger condition to activate command stacks on one of the master
    trigger signals.

  - The total number of MVLC command stacks has been raised from 8 to 16, so now
    15 command stacks are available for readout events.

* mvlc:

  - The default Trigger I/O script is now empty as the previous default script
    was confusing.

  - Remember last connected ETH hostname and other settings when switching
    controller types.

  - Add more utility VME scripts for the MVLC related to multicrate master/slave
    handling.

  - Trigger I/O DSO: Fix simulation of strobed LUT outputs.

* analysis:

  - histo1d: log scale plotting and stat calculation fixes

  - ExportSink: updates to the generated code: port to python3, require
    cmake 3.0, suppress compiler warnings, do not force c++14 but pick the
    standard used by ROOT instead. Tested with python-3.11.

* Only attempt to connect to VME controllers once instead of multiple times to
  reduce log spam and wait time in case of errors.

* Crash fix with old mvmelst formatted data and multi event splitting.

Version 1.10.4
--------------

* analysis: Fix bug leading to ExportSink output files being truncated on listfile load.

Version 1.10.3
--------------

* mvlc: Fix ``mblts`` and ``mbltsfifo`` commands producing broken readout stacks.

* analysis:

  - correctly set the output limits of the ``Previous Value`` operator

  - allow toggling between scientific and full number display in the "Show
    Parameters" window.

Version 1.10.2
--------------

* Fix large values for listfile split size and split duration being truncated.

Version 1.10.1
--------------

* Add the prometheus+grafana docker-compose project to the binary packages
  (extras/metrics).

Version 1.10.0
--------------

* new: filter listfiles based on analysis conditions. Produces MVLC_USB
  formatted output listfiles.

* new: prometheus metrics for the MVLC readout and the analysis. Metrics
  are exposed on port 13802 by default.

* vme_templates: Updated VMMR template. Merge vmmr and vmmr_1ns variants.

* MVLC Trigger IO: fixes for the LUT simulation code (FW0036 related).

* analysis: conditions now output 0.0 if false instead of invalid_param()/NaN.
  Allows to track condition true/false and total counts in a single 1d
  histogram.

* bugfixes: empty listfile filename display, uninitialized data in multi_event_splitter

Version 1.9.2
-------------

* mvlc: abort daq start sequence on error in trigger io init.

* histo2d: enable slicing only for 2d histos, not for 1d array views

Version 1.9.1
-------------

* vme_script: Add new ``mvme_require_version`` command: software side check of
  minimum mvme version required to run the script.

* MVLC Trigger IO

  - Add ``mvme_require_version 1.9.1`` at the top of the script.
    Older versions cannot parse the updated format (``mvlc_stack_begin/end``
    blocks) correctly. They will now error out due to the unknown
    ``mvme_require_version`` command.

  - Trigger IO script exec fixes: the standard ``run_script`` function is now
    used to run the script.

* mvlc: Fix bogus value of ``moduleData.hasDynamic`` in mvlc_readout_parser.

Version 1.9.0
-------------

* Major mesytec-mvlc update for MVLC FW0036 and later:

  - MVLC now supports the FIFO flag for block reads. The
    ``mvlc_set_address_inc_mode`` command has been removed. New VME Script
    commands have been added instead (see below).

  - Larger command stack uploads are now possible. The stack is uploaded in
    parts. The max size of each part depends on the transport being used: ETH is
    limited by the max UDP packet size, USB by MVLC internal buffer sizes.

* vme_script: Implement new commands for 2eSST fifo and memory block reads:
  ``2esstfifo``, ``2esstsfifo``, ``2esstmem``, ``2esstsmem``.

* vme_script: Better error handling and log output for MVLC inline stacks.

* MVLC Trigger IO: unit initialization is now wrapped in ``mvlc_stack_begin/end``
  blocks to get atomic init behavior. This means executing the Trigger IO script
  won't interfere with the DSO or active readouts that are using the Trigger IO
  system. This change also speeds up execution of the Trigger IO init script.
  Note: the Trigger IO script has to be regenerated via the GUI for this change
  to take effect!

* vme_script: Better error handling and log output for MVLC inline stacks
  (``mvlc_stack_begin/end``).

* MVLC Trigger IO: unit initialization is now wrapped in ``mvlc_stack_begin/end``
  blocks to get atomic init behavior. This means executing the Trigger IO script
  won't interfere with the DSO or active readouts that are using the Trigger IO
  system. This change also speeds up execution of the Trigger IO init script.
  Note: the Trigger IO script has to be regenerated via the GUI for this change
  to take effect!

* MVLC DSO

  - Fix DSO readout returning early before having received a trigger.

  - DSO readout does not use an internal timeout anymore. This means
    pulses with very long interval times can now be reliably sampled.

  - Rework the UI: can now enter measurement duration instead of post-trigger
    time. Max measurement duration is limited to 65500 ns by the MVLC.

  - Plot: Fix issue where the trigger edge was not aligned with the 0
    coordinate.

* Implement 2D Histogram slicing. Works for X and Y and uses the currently
  visible area. The slices are opened in a new 1D histogram window.

* vme_templates: Add hardware id checks for mesytec modules similar to MDPP-16
  firmware type checks.

* Merge PR from wvonseeg to make the sparse ExportSink python code work with
  python-3.10.

* Use FIFO block reads in VME Debug Widget.


Version 1.8.2
-------------
* Better fix for the EventServer reconnect race: clients are not disconnected
  anymore when loading listfiles or switching VME controllers. Also remove the
  sleep from mvme_jsonrpc_replay.py

* Readd the ``mvme.sh`` script to directly start mvme with the correct env
  variables set. Note: mvme.sh sources the ``initMVME`` script to setup the
  environment.

Version 1.8.1
-------------

* mvme_root_client: Abort if the DAQ run/replay is already in progress whenn
  connecting. Can be disabled by passing "run-in-progress-is-ok" on the command
  line.

* mvme_jsonrpc_replay.py: Sleep between loading a listfile and starting the
  replay. This works around a race where the mvme_root_client was not connected
  yet but the replay was already running.

Version 1.8.0
-------------

* [mesytec-mvlc]

  - mvlc_eth: Do not send a frame to the data pipe when connecting. This way
    ongoing readouts won't be redirected when a second process connects to the
    MVLC.

  - eth and usb: Do not reset the stack trigger registers when connecting. It
    made reading back the last trigger configuration impossible. Now only the DAQ
    mode register is written when requested via disableTriggersOnConnect().

  - New SplitZipReader to replay from split listfiles stored across multiple zip
    archives. To consumers it looks like the data came from a single file.

    Replaying all parts from a split listfile is done in the 'listfile browser
    (ctrl+4)' by checking 'replay all parts' before opening the first part that
    should be replayed.

* [vme_templates] Update integration parameter ranges for MDPP-16/32-QDC

* When a listfile is opened do not try to auto connect to the VME controller.

* Updates to the JSON-RPC listfile handling methods: 'loadAnalyis',
  'keepHistoContents' and 'replayAllParts' are now explicit parameters to the
  respective methods.

* The Qt Assistant binary is now again contained with the linux package.


Version 1.7.2-1
---------------

* Use current workspace directory as the starting point for MVLC CrateConfig
  exports.

Version 1.7.2
-------------

* Fix mvme_root_client compilation issue against root 6.22.06

* New JSON-RPC remote control methods for loading analysis configs and opening
  listfiles.

  extras/mvme_jsonrpc_replay.py shows how to replay from a list of input
  listmode files while accumulating into the same analysis.

* Close projection plots when the parent h2d plot is closed.

* Better error logging in multi_event_splitter.

* Fix 'read_to_accu' missing the 'late' flag when exporting a VMEConfig to
  mesytec-mvlc CrateConfig.

Version 1.7.1
-------------

* [analysis]

  - Show module/group names in readout parser debug.

  - Improve histo stats widget table formatting and show the RMS value of each
    column.

  - Fix 1D histo statistics not following the zoom under Windows.

  - Clear 2D histograms when their subrange was modified.

  - Replace the histo resolution slider with a combo box.

  - Interval condition can now exclude/ignore specific intervals from affecting
    the conditions result.

  - Many improvements to graphical interval/polygon condition editing.

  - Dependency Graph View now starts editing data sources on double-click.

  - Fix a source of frequent crashes when modifying the analysis (periodic histo
    counter updates).

* [vme_templates]

  - Slightly improve the bus_time filters for VMMR modules

  - Calibrate mesytec vme module timestamps to µs.

* [mvlc]

  - DSO plot and logic updates (recommended to use MVLC Firmware FW0031 or later).

  - Start/stop the DSO using a single stack transaction instead of multiple individual
    commands. Fixes issues when the DSO is running while the Triger IO script is being
    written to the MVLC.

* [doc]

  - Update to the "Manual ARP setup" section for the MVLC.

* [packaging]

  - Add missing graphviz dependencies to the linux packages.

Version 1.7.0
-------------

* [vme_script]

  - Breaking change: spaces are not allowed in variables names anymore. The UI
    now also rejects attempts to uses spaces in variable names.

  - Can now place complete vme_script command lines in variables, e.g.: ::

        set readout_cmd "mbltfifo a32 0x0100 65535"
        ${readout_cmd}

    The second line above is now correctly parsed as a **mbltfifo** command.

    Variable references can also be used on the right-hand side: ::

        set my_addr 0x1234
        set readout_cmd "mbltfifo a32 ${my_addr} 65535"
        ${readout_cmd} # Will be expanded to "mbltfifo a32 0x1234 65535"

    This process is not recursive.

* [ui]

  - Save/restore node expansion state of the VME Config tree.

  - VME script editor: add new "Run (ignore errors)" action. Useful for
    temporarily ignoring errors from VME scripts and running the script to the
    end.

  - Remove **BerrMarker** and **EoMMarker** text from buffer debut output. These
    values were only added for the VM-USB and are misleading when looking at MVLC
    buffers.

  - Show RMS value in 1d plot grid tiles.

* [mvme_root_client]

  Breaking change: improve handling of TTrees split across multiple files.

  The *TTree::SetMaxTreeSize()* can now be specified on the command line when
  recording: *--root-max-tree-size=<maxBytesPerFile>*. The default value is set
  to the ROOT default of 100000000000LL.

  Replay mode is now enabled via *--replay*. In this mode mvme_root_client now
  accepts a list of filenames instead of a single file. The filenames are used
  to create a TChain object which becomes the source for the replay data.

Version 1.6.3
-------------

* Another mvme_root_client compilation fix.

Version 1.6.2
-------------

* vme_templates: Add support for the MVHV-4 VME High Voltage Bias Supply

* Fix mvme_root_client compilation issue: do not set c++ standard in the Makefile.

* Packaging: do not package libz.so anymore.


Version 1.6.1
-------------

* [gui]

  - New feature: recover corrupted listfiles.

    If a listfile ZIP archive is corrupted due to a crash/power outage the UI
    now offers a way to attempt to recover the data when opening the corrupted
    archive.

    Recovery works by searching for the first local file header in the zip
    archive and attempting to unpack the following data. The recovery process
    also works for listfile archives containing LZ4 compressed readout data.

  - New feature: can now save/load VME event configs  to/from file

    Saving is done via the events context menu entry "Save Event to file".

    To load an event and add it as a new event use the top-level "Events" node
    context menu and select "Add Event from file".

    Saved events can also be merged into existing events: Use "Merge with Event
    from file" from the destination events context menu. This will add all
    modules from the source event to the target event. Non-system and
    non-mesytec VME Script variables defined in the source event will be added
    to the destination event. Existing variables are overwritten.

  - add "Save Script" to the VME tree context menu

  - Do not allow deleting the MVLC Trigger/IO script

  - Fix file saving logic across the GUI. The logic was flawed and could lead to
    files being overwritten.

* [vme/readout]

  - Return earlier if errors occur during the DAQ start sequence. Return points
    are: after global start scripts, after VME module init scripts and after event
    start scripts.

  - Update module template for the MDLL: init script udpates and analaysis
    filter and naming fixes.

* [analysis]

  - Implement on-the-fly histogram creation when attempting to graphically edit
    a condition that does not have a matching histogram.

  - Increase initial size of plot windows so that all toolbar buttons are
    visible (hopefully).

  - Crash fix in the ExportSink operator UI.

* [doc] Changelog was missing from PDF file in windows builds.

* [mesytec-mvlc]

  - Add a command line vme-scan-bus tool. This is in its early stages and needs
    more polish.

* Updated build system for linux binaries: Debian Stretch with glibc-2.24 is
  used with custom built gcc-10.4 and Qt-5.15.8 libraries. Deployment is done
  using 'linuxdeployqt'.

  The binaries should now run on a wider range of systems (all using
  glibc>=2.24) while still containing a modern version of Qt. A detailed list
  of glibc versions used in common distributions can be found here:
  https://repology.org/project/glibc/versions


Version 1.6.0
-------------

* [analysis]

  - Add plot grid views: configurable window for showing multiple plots in a
    grid layout.

  - Reworked the 1d histogram statistics window: it now uses a table to display
    the data and the statistics are synchronized to the zoomed area of the
    histogram widget.

  - Add multi_event_splitter counter output to the analysis info widget.

* [vme_script]

  - VME amod parsing is not case-sensitive anymore. By default the
    user/non-privileged VME amods are used but numeric amod arguments are now
    also accepted to allow full control of the amod.

  - The effective vme amod value is now logged in the output of script commands.

  - read and readabs now accept "late" in addition to "slow"

  - Improve the script level accumulator commands to make them similar to the
    MVLC accu stack commands.

* Fix VME Debug Widget block reads not working anymore (wrong VME amod was used)

* mvme now requires c++17!


Version 1.5.0
-------------

* [analysis]

  - Implemented a :ref:`condition system <analysis-condition-system>` and
    1d-interval, 2d-polygon and expression (exprtk) conditions.

  - Added a new :ref:`dependency viewer <analysis-dependency-graph>` to
    visualize data processing and active conditions.

* [vme/readout]

  - Revert a change from 1.4.9 where lowercase amod specifiers used the
    *privileged* value, while uppercase specifiers where converted to the *user*
    value. Now by default the user amods are used but numeric amod arguments can
    be given to single and block read commands for full control over the amod.

  - Add the raw VME amod value to the log output of vme script commands.

  - Add new commands for the fast 2eSST VME transfer modes:
    :ref:`2esst <vme-command-2esst>` and the word swapped version
    :ref:`2essts <vme-command-2essts>`.

  - Add new module templates for mesytec MDLL, mesytec MCPD-8_MPSD and the CAEN v1742

  - Add a new software accumulator and related functions:
    :ref:`accu_set <vme-command-accu-set>`,
    :ref:`accu_mask_rotate <vme-command-accu-mask-rotate>`,
    :ref:`accu_test <vme-command-accu-test>`

  - Update MDPP-16/32 scripts to check if the correct firmware revision is loaded.

  - Listfile filenames can now be specified using format strings (fmt library).

Version 1.4.9.5
---------------

* Bugfix release: listfile archives where missing the analysis config and log file.

Version 1.4.9.4
---------------

* Fix data rate monitoring and display when using MVLC_USB (read timeout issue)

Version 1.4.9.3
---------------

* Improved listfile filename generation: an fmt format string can now be used to
  specify the output filename. Currently the run number and the timestamp are passed
  as arguments when generating the output filename.

* Add untested templates for the CAEN v775 TDC module.

Version 1.4.9.2
---------------

* [analysis] Suppress completely empty events when using the SIS3153 controller.

Version 1.4.9
-------------
* [analysis]

  - Add a new MultiHitExtractor data source allowing to extract multiple hits
    per address.

  - Add 'Generate Histograms' context menu action to data sources and operators
    to quickly generate histograms for selected objects.

  - Raise maximum number of data sources and operators per VME event context
    from 256 to 65536.

  - Improve histo1d stats output.

* New feature: listfile splitting (MVLC only)

  When recording readout data the output listfile can now be split either based
  on file size or elapsed time. Each partial listfile ZIP archive is in itself
  a complete, valid mvme listfile and includes the VME config, analysis config
  and logged messages.

  Replaying from split listfiles currently has to be done manually for each
  part. Using the 'keep histo contents' in mvme allows to accumulate data from
  multiple (partial) listfiles into the same analysis.

* Listfile output directory can now be selected in the Workspace Settings GUI.

* Add new optional suffix part to listfile filename generation.

* New feature: VME modules can now be saved to and loaded from JSON files. This
  can be used to create custom VME modules without having to use the mvme VME
  template system.

* DAQ run number is now incremented on MVLC readout stop to represent the *next*
  run number.

* Show the original incoming data rate in the analysis window when replaying
  from listfile.

* VME Config: allow moving modules between VME Events via drag&drop.

* [mvlc]

  - Revert the MVLC readout parser simplification done in 1.4.8

    The parser now allows prefix, dynamic and suffix parts again. The parser data
    callback remains unchanged, passing the parsed data as a single pointer +
    size.

  - Fix command timeout errors with older USB2 chipsets.

  - Fix USB2 connection issues by retrying opening the device.

  - Periodically add stack error information received on the command pipe to
    recorded listfile data. Uses a new system_event::StackErrors section to
    store stack error locations, flags and counts.

  - Fix 'VME Script -> Run' in the MVLC Debug GUI

* [vme_templates]

  - Add 'stop_acquisition', 'reset_fifo' and 'readout_reset' commands to
    mesytec module reset scripts. Fixes an issue where the modules could signal
    a VME IRQ during the init sequence but before the DAQ was properly started
    with the multicast start sequence.

  - Improve Triva7 VME module templates.

* Improved VME Script Execution: log messages from commands are now immediately
  visible. Progress dialog shows progress based on number of commands.

* Fix wrong VME -> analysis module assignments when disabled VME modules are
  present in the config.

* New ZMQ publisher listfile output (MVLC only).

  Sends readout buffers through a ZMQ PUB socket. Based on code from GANIL.


Version 1.4.8.2
---------------
Raise MVLC command timeout (request/response) from 500ms to 1000ms.

Version 1.4.8.1
---------------
Make mvme build against qwt versions older than 6.2.0 again.

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
