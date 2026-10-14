.. index:: Data Export, CSV, Listfile Export, mvme_listfile_export
.. _reference-listfile_export:

Listfile export tool
====================
``mvme_listfile_export`` is a command line program shipped with mvme. It reads
an MVLC listfile and writes selected module data arrays to a CSV or plain text
file. The output contains one row per readout event and one column per array
element. The mvme GUI and the analysis are not involved.

The tool is installed into the ``bin`` directory of the mvme installation.

Data extraction uses the default data filters from the mvme module templates.
The values written are the raw extracted data values. Calibrations and other
processing done in the mvme analysis are not applied.

Usage
-----

::

   mvme_listfile_export explore <listfile.zip>
   mvme_listfile_export export  <listfile.zip> [options] [<array>...]

explore
~~~~~~~
Shows listfile meta information and all VME events, modules and data arrays
available for export. Each array is printed with its full path, its size and
the data filter used to extract it.

For each module the tool prints how its module type was determined:

* ``matched by vme_module_type``: from module type information stored in the
  MVLC crate config.
* ``matched by mvme config``: from the mvme VME config stored in the listfile.
* ``matched by module name``: fallback, the module name contains a known module
  type name.
* ``unknown module type`` / ``no module type info``: no filters available, the
  module has no exportable arrays.

export
~~~~~~
Writes the selected arrays to the output file. Arrays are selected by their
full path as shown by ``explore``: ``<event>.<module>.<array>``. Columns are
written in the order the arrays are given on the command line.

Single elements are selected by appending a comma separated list of indices
and inclusive ranges in brackets, e.g. ``my_event.my_mdpp16.amplitude[0-3,8]``.
Quote these selectors to prevent shell globbing.

All selected arrays must belong to the same VME event. If no array is given and
the listfile contains exactly one module with data, the first array of that
module is exported.

Column names in the header line are the array paths with the element index
appended, e.g. ``event0.mdpp16_0.amplitude[3]``.

Options
~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Option
     - Description
   * - ``-o``, ``--output <file>``
     - Output file, ``-`` for stdout. Default: the listfile name with a
       ``.csv`` or ``.txt`` extension.
   * - ``--format <csv|text>``
     - ``csv``: comma separated, missing values are empty.
       ``text``: space separated, missing values are ``nan``.
       Default: ``text`` if the output file ends in ``.txt`` or ``.dat``,
       otherwise ``csv``.
   * - ``--separator <sep>``
     - Override the column separator. Any string, ``\t`` or ``tab`` for a tab
       character.
   * - ``--missing <nan|empty|0>``
     - Override the representation of missing values.
   * - ``--skip-empty-rows``
     - Do not write events where all selected values are missing.
   * - ``--no-header``
     - Do not write the column name header line.
   * - ``--log-level <level>``
     - One of ``trace``, ``debug``, ``info``, ``warn``, ``error``, ``off``.
   * - ``-h``, ``--help``
     - Show the usage text.

Exported data goes to the output file or stdout. Log and status messages are
written to stderr.

Missing values
~~~~~~~~~~~~~~
An array element is missing if the module did not produce data for it in an
event, e.g. a channel below threshold. Missing values are written as empty
fields (csv) or ``nan`` (text) and are never replaced by ``0`` unless
``--missing 0`` is given.

Events where all selected values are missing are written by default and counted
in the summary printed at the end of the export. Such events usually indicate
trigger problems. Use ``--skip-empty-rows`` to suppress them.

Limitations
-----------
* Only MVLC listfiles are supported. Listfiles from other VME controllers are
  rejected.
* Single crate listfiles only.
* All selected arrays must belong to the same VME event.
* Multi event readout is not supported. If a channel is hit more than once
  within one readout event, only the last value is kept and a warning is
  printed at the end of the export.

Example
-------
The listfile in this example contains one VME event with several MDPP-16 SCP
modules. The goal is to export the amplitudes of channels 0 to 3 and channel
8 of the module ``mdpp16_scp_0``.

First list the available arrays:

::

   $ mvme_listfile_export explore mdpp16_scp-pulser.zip
   Listfile: mdpp16_scp-pulser.zip
     archive entries: mdpp16_scp-pulser.mvlclst.lz4, messages.log, analysis.analysis, mvme_run_notes.txt
     listfile entry:  mdpp16_scp-pulser.mvlclst.lz4
     format:          MVLC ETH
     preamble:        EndianMarker, MVLCCrateConfig, MVMEConfig, BeginRun
     crate id:        0

   event[0] event0
     module[0] mdpp16_scp_0: type=mdpp16_scp, matched by mvme config
       event0.mdpp16_scp_0.amplitude                    size=16   filter=0001XXXXPO00AAAADDDDDDDDDDDDDDDD
       event0.mdpp16_scp_0.channel_time                 size=16   filter=0001XXXXXX01AAAADDDDDDDDDDDDDDDD
       event0.mdpp16_scp_0.trigger_time                 size=2    filter=0001XXXXXX10000ADDDDDDDDDDDDDDDD
       event0.mdpp16_scp_0.module_timestamp             size=1    filter=11DDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
     module[1] mdpp16_scp_1: type=mdpp16_scp, matched by mvme config
       ...

Then export the selected channels to a space separated text file:

::

   $ mvme_listfile_export export mdpp16_scp-pulser.zip -o amplitudes.txt 'event0.mdpp16_scp_0.amplitude[0-3,8]'
   [info] writing 5 columns to amplitudes.txt
   [info] read 63.38 MiB, 37016 events, 37016 rows written, elapsed=0.21 s, rate=301.32 MiB/s
   Wrote 37016 rows to amplitudes.txt. Events: 37016, events without data: 0

   $ head -4 amplitudes.txt
   event0.mdpp16_scp_0.amplitude[0] event0.mdpp16_scp_0.amplitude[1] event0.mdpp16_scp_0.amplitude[2] event0.mdpp16_scp_0.amplitude[3] event0.mdpp16_scp_0.amplitude[8]
   5286 5230 5320 5310 5233
   5282 5230 5320 5312 5235
   5282 5231 5316 5307 5232

To export all 16 amplitudes as CSV, select the whole array and let the tool
derive the output filename (``mdpp16_scp-pulser.csv``):

::

   $ mvme_listfile_export export mdpp16_scp-pulser.zip event0.mdpp16_scp_0.amplitude

The output can also be piped into other programs:

::

   $ mvme_listfile_export export mdpp16_scp-pulser.zip -o - --no-header event0.mdpp16_scp_0.amplitude | my_analysis
