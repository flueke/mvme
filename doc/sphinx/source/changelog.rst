##################################################
Changelog
##################################################

Current
-------

* Support for the Struck SIS3153 VME Controller over ethernet
* Analysis:
  - Performance improvments
  - Better statistics
  - Can now single step through events to ease debugging
  - Add additional analysis aggregate operations: min, max, mean, sigma in x
    and y
  - Save/load of complete analysis sessions: Histogram contents are saved to
    disk and can be loaded at a later time. No new replay of the data is
    neccessary.
* Improved mesytec vme module templates. Also added templates for the new VMMR
  module.
* More options on how the output listfile names are generated.
* Various bugfixes and improvements

0.9.2
-------

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

0.9.1
-------

* Record a timetick every second. Timeticks are stored as sections in the
  listfile and are passed to the analyis during DAQ and replay.
* Add option to keep histo data across runs/replays
* Fixes to histograms with axis unit values >= 2^31
* Always use ZIP format for listfiles
