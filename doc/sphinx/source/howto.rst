.. _howto:

##################################################
How-To Guides
##################################################

.. _howto-rate-estimation:

==================================================
Rate Estimation Setup
==================================================

.. warning::
    * FIXME: how to figure out the calibration of the timestamp data?
    * FIXME: special settings needed for the module? counter vs timestamp?
    * Missing final screenshot.
    * Missing explanation about what's being calculated.

This example shows how to use the rate estimation feature built into 1D
histograms. An MDPP-16_SCP (called *mdpp16*) is used here but any mesytec VME
module should work. The rate estimation is setup for channel 8 of the MDPP.

To make use of the rate estimation feature a 1D histogram accumulating the
difference between timestamp values of incoming events needs to be created.

Steps for creating the histogram:

Timestamp extraction
--------------------

Add a new Extraction Filter with two filter words to the mdpp16: ::

  0001 XXXX XX00 1000 XXXX XXXX XXXX XXXX     any
  11XX XXXX XXXX XDDD DDDD DDDD DDDD DDDD     any

The first filter word matches on the data word for channel 8: the prefix
``0001`` identifies a data word, the pattern ``00 1000`` is used to select
channel 8 specifically.

The second data word is used to extract the timestamp value. The prefix ``11``
marks an *End of Event* word which contains the 30-bit timestamp. Using the
``D`` character 19-bits of the timestamp value are extracted.

Set the name of the filter to *ts_c8* and click *Ok* to create the filter.

The complete filter will thus match if there was a data word for channel 8 and
the event contained a timestamp data word.

Making the previous timestamp available
---------------------------------------

Next right-click in the *L1 Processing* tree (or any higher level processing
tree) and select *New -> Previous Value*. Use the green *+* button in the
top-right corner to add a new user level if necessary.

As input select the *ts_c8* node created in the previous step. Make sure the
*Keep valid parameters* box is checked. Set the name to *ts_c8.prev*.

Histogramming the timestamp difference
--------------------------------------

Again right-click in one of the processing trees and choose *New ->
Difference*. Set input A to *ts_c8* and input B to *ts_c8.prev*. Set the name
to *ts_c8.diff*.

.. autofigure:: images/guide_rateEstimation_add_difference.png

    Calculating the timestamp difference


Right-click in the display tree below and add a new 1D histogram. Select the
difference *ts_c8_diff* as the input.

Open the newly created histogram click on *Subrange*, select *Limit X-Axis* and
enter ``(-100.0, 4000.0)`` as the limits. This step limits the large default
parameter range calculated by the :ref:`Difference operator <analysis-Difference>`.

.. autofigure:: images/guide_rateEstimation_set_histo_limits.png

    Setting the histogram subrange

Next click the *Rate Estimation* button in the toolbar and then select two
points on the x-axis to use for the rate estimation.

.. autofigure:: images/guide_rateEstimation_select_estimation_points.png

    Selecting two x-coordinates for the rate estimation

.. H1D: ts_diff_C8
.. Diff: ts_diff_C8 = chan8_ts - chan8_ts_prev
.. chan8_ts_prev.keepValid = true
