.. _howto:

##################################################
How-To Guides
##################################################

.. _howto-rate-estimation:

==================================================
Rate Estimation Setup
==================================================

This example shows how to use the rate estimation feature built into 1D
histograms. Rate estimation works with statistical data that forms an
exponential function.

In this example and MDPP-16_SCP is used but any mesytec VME module should work.
The rate estimation is setup for channel 8 of the MDPP.

To make use of the rate estimation feature a 1D histogram accumulating the
:ref:`difference <analysis-Difference>` between timestamp values of incoming
events needs to be created.

.. note::

    For the module to produce timestamps instead of event counter values the
    register ``0x6038`` needs to be set to ``1``. mvme does this by default for
    newly created modules in the *VME Interface Settings* script.

Steps for creating the histogram:

.. _howto-rate-estimation-ts-extraction:

Timestamp extraction
--------------------

.. highlight:: none

Add a new Extraction Filter with two filter words to the mdpp16: ::

  0001 XXXX XX00 1000 XXXX XXXX XXXX XXXX     any
  11XX XXXX XXXX XDDD DDDD DDDD DDDD DDDD     any

The first filter word matches on the data word for channel 8: the prefix
``0001`` identifies a data word, the pattern ``00 1000`` is used to select
channel 8 specifically.

The second filter word is used to extract the timestamp value. The prefix ``11``
marks an *End of Event* word which contains the 30-bit timestamp. Using the
``D`` character 19-bits of the timestamp value are extracted.

Set the name of the filter to *ts_c8* and click *Ok* to create the filter.

The complete filter will thus match if there was a data word for channel 8 and
the event contained a timestamp data word.

Timestamp calibration
---------------------
By default the timestamp is generated using the 16 MHz VME SYSCLK. This means
the 19-bit timestamp value is in units of 16 MHz.

The goal is to convert the value to *µs* which is easier to use: :math:`2^{19}
/ 16 = 2^{19} / 2^{4} = 2^{15} = 32768`.

Thus instead of ranging from :math:`\left[ 0, 2^{19} \right[` the timestamp
should use the range :math:`\left[ 0, 2^{15} \right[`.

To transform the value create a :ref:`Calibration Operator
<analysis-Calibration>` by right-clicking inside the *L1 Processing* tree (or
any higher level processing tree) and selecting *New -> Calibration*. Use the
green *+* button in the top-right corner to add a new user level if necessary.

As input select the *ts_c8* node created in the previous step. Keep the default
name *ts_c8.cal*. Type ``µs`` in the *Unit Label* field, set *Unit Max* to
``32768`` and press the *Apply* button. Accept the dialog by pressing *Ok*.

.. autofigure:: images/guide_rateEstimation_add_calibration.png

    Timestamp calibration

Making the previous timestamp available
---------------------------------------

Next right-click in the *L1 Processing* tree (or any higher level processing
tree) and select *New -> Previous Value*.

As input select the *ts_c8.cal* node created in the previous step. Make sure the
*Keep valid parameters* box is checked. Set the name to *ts_c8.cal.prev*.

.. autofigure:: images/guide_rateEstimation_add_previousvalue.png

    Adding the PreviousValue operator

Histogramming the timestamp difference
--------------------------------------

Again right-click in one of the processing trees and choose *New ->
Difference*. Set input A to *ts_c8.cal* and input B to *ts_c8.cal.prev*. Set
the name to *ts_c8.diff*.

.. autofigure:: images/guide_rateEstimation_add_difference.png

    Calculating the timestamp difference


Right-click in the display tree below and add a new 1D histogram using the
difference *ts_c8.diff* as the input.

Open the newly created histogram click on *Subrange*, select *Limit X-Axis* and
enter ``(0.0, 200.0)`` as the limits. This step limits the large default
parameter range calculated by the :ref:`Difference operator <analysis-Difference>`.

.. autofigure:: images/guide_rateEstimation_set_histo_limits.png

    Setting the histogram subrange

Next click the *Rate Estimation* button in the toolbar and then select two
points on the x-axis to use for the rate estimation.

.. autofigure:: images/guide_rateEstimation_select_estimation_points.png

    Rate estimation data and curve visible

The calculation performed is:

.. math::

    \tau     &= (x_{2} - x_{1}) / log(y_{1} / y_{2}) \\
    y        &= y_{1} * (e^{-x / \tau} / e^{-x_{1} / \tau}) \\
    freeRate &= 1.0 / \tau


.. _howto-vmusb-firmware-update:

==================================================
VM-USB Firmware Update
==================================================

The VM-USB firmware update functionality can be found in the mvme main window
under *Tools -> VM-USB Firmware Update*. The latest firmware file is included
in the mvme installation directory under *extras/vm-usb*.

Before starting the update set the *Prog* dial on the VM-USB to one of the
programming positions P1-P4.

The controller will start the newly written firmware immediately after writing
completes. Reset the *Prog* dial to C1-C4 to make the controller start the
correct firmware on the next power cycle.

.. .. _howto-debugging:

.. ==================================================
.. Debugging techniques
.. ==================================================

.. TODO:
..     * DAQ: 1 Cycle and buffer dump to console
..     * Listfile: 1 Event / Next Event and buffer dump
..     * VME Debug Window (``Ctrl+4``)
..     * Run Script and it's output
..     * Analysis: Show Parameters
