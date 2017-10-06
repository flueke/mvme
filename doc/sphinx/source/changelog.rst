##################################################
Changelog
##################################################

0.9.3

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

* Record a timetick every second. Timeticks are stored as sections in the
  listfile and are passed to the analyis during DAQ and replay.
* Add option to keep histo data across runs/replays
* Fixes to histograms with axis unit values >= 2^31
* Always use ZIP format for listfiles
