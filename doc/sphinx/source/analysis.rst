==================================================
Analysis
==================================================

.. _analysis-ui:
.. _analysis-user-guide:

User Guide
----------------------------------------

UI Overview
~~~~~~~~~~~

The Analysis system in mvme is designed to allow

* flexible parameter extraction from raw readout data.
* calibration and additional processing of extracted parameters.
* accumulation and visualization of processed data.

The user interface follows the structure of data flow in the system: the
modules for the selected *Event* are shown in the top-left tree.

The bottom-left tree contains the *raw histograms* used to accumulate the
unmodified data extracted from the modules.

The next column (*Level 1*) contains calibration operators in the top view and
calibrated histograms in the bottom view.

.. _analysis-ui-block-diagram:

.. autofigure:: images/analysis_ui_block_diagram.png
    :scale-latex: 100%

    Analysis UI Block Diagram


.. note::
    To get a set of basic data extraction filters, calibration operators and
    histograms right-click on a module an select *Generate default filters*.

Levels are used to structure the analysis and it's completely optional to have
more than two of them. Additional levels can be added/removed using the *+* and
*-* buttons at the top-right.  Use the *Level Visibility* button to select
which levels are visible.

The UI enforces the rule that operators can use inputs from levels less-than or
equal to their own level. This means data should always flow from left to
right.

Operators can be moved between levels by dragging and dropping them.


.. _analysis-ui-screenshot:

.. autofigure:: images/analysis_ui_simple_io_highlights.png
    :scale-latex: 100%

    Analysis UI Screenshot

    The Calibration for *mdpp16.amplitude*  is selected. Its input is shown in
    green. Operators using the calibrated data are shown in a light blueish
    color.

Selecting an object will highlight its input data sources in green and any
operators using its output in blue.

Adding new objects
~~~~~~~~~~~~~~~~~~

Right-click in any of the views and select *New* to add new operators and
histograms. A dialog will pop up with input fields for operator specific
settings and buttons to select the operators inputs.

Clicking any of the input buttons will make the user interface enter "input
select mode". In this mode valid outputs for the selected input are
highlighted.

.. autofigure:: images/analysis_ui_add_op_input_select.png
    :scale-latex: 100%

    Input select mode

    Adding a :ref:`analysis-RangeFilter1D` to Level 2. Valid inputs are
    highlighted in green.

Click an input node to use it for the new operator. If required fill in any
additional operator specific data and accept the dialog. The operator will be
added to the system and will immediately start processing data if a DAQ run or
replay is active.

For details about data extraction refer to :ref:`analysis-sources`.
Descriptions of available operators can be found in :ref:`analysis-operators`.
For details about 1D and 2D histograms check the :ref:`analysis-sinks` section.


System Details
----------------------------------------

As outlined in the :ref:`introduction <intro-analysis>` the analysis system is
a set of interconnected objects with data flowing from :ref:`Sources
<analysis-sources>` through :ref:`Operators <analysis-operators>` into
:ref:`Sinks <analysis-sinks>`.

The system is structured the same way as the VME Configuration: VME modules are
grouped into events. An event contains the modules that are read out on
activation of a certain trigger condition. The result of the readout is the
modules event data (basically an array of 32-bit words). This module event data
is the input to the analysis system.

When processing data from a live DAQ run or from a listfile replay the analysis
system is "stepped" in terms of events: in each step all the
:ref:`analysis-sources` attached to a module get passed the modules event data.
The task of each source is to extract relevant values from its input data and
make these values available to subsequent operators and sinks.

.. FIXME: What is the correct order?

After all sources have processed the module event data the dependent operators
and sinks are stepped in the correct order. Each object consumes its input and
generates new output or in the case of sinks accumulates incoming data into a
histogram.

.. figure:: images/analysis_flowchart.png

    Example analysis dataflow

Parameter Arrays
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The transport container carrying data between objects is the Parameter Array:

+-----------------+------------+-------+--------+
| **Parameter Array**                           |
+=================+============+=======+========+
| size            | unit label                  |
+-----------------+------------+-------+--------+
| **Parameters**                                |
+-----------------+------------+-------+--------+
| 0               | value      | valid | limits |
+-----------------+------------+-------+--------+
| 1               | value      | valid | limits |
+-----------------+------------+-------+--------+
| 2               | value      | valid | limits |
+-----------------+------------+-------+--------+
| \.\.\.          |            |       |        |
+-----------------+------------+-------+--------+
| *size-1*        | value      | valid | limits |
+-----------------+------------+-------+--------+

The *size* of parameter arrays is determined at analysis startup time and is
constant throughout the run. The *unit label* is a string which currently can
be set through the use of the :ref:`Calibration Operator
<analysis-calibration>`. The index of a parameter in the array is usually the
channel address that was extracted from the modules data.

Each parameter has the following attributes:

* *value* (double)

  The parameters data value.

* *valid* (bool)

  True if the parameter is considered valid, false otherwise.

  A parameter can become invalid if for example a data source did not extract a
  value for the corresponding channel address or an operator wants to
  explicitly filter out the address or could not calculate a valid result for
  the input value.

* *limits* (two doubles)

  Two double values forming the interval ``[lowerLimit, upperLimit)`` that the
  parameters value should fall into. This is used by histogram sinks and
  calibration operators to determine the parameters range and thus calculate
  the binning.

Connection types
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Different operators have different requirements on their input types. The
:ref:`Calibration Operator <analysis-calibration>` for example can use whole
parameter arrays as its input, transforms each data value and produces an
output array of the same size as the input size.

Other operators can only act on individual values and thus connect directly to
a specific *index* into the parameter array. An example is the :ref:`2D
Histogram Sink <analysis-histo2dsink>`: it requires exactly two input values, X
and Y, neither of which can be an array.

.. figure:: images/analysis_input_types.png

   Example of different input types

Each Operator implementation decides which types of input connections it
accepts. Some operators even change the type of inputs they accept based on the
first input type that is connected (they either accept full arrays for all
their inputs or single values for all their inputs).

The :ref:`Analysis UI <analysis-ui>` will highlight valid input nodes in green
when selecting an operators input.

.. _analysis-sources:

Data Sources
----------------------------------------
Analysis Data Sources attach directly to a VME module. On every step of the
analysis system they're handed all the data words produced by that module in
the corresponding readout cycle. Their job is to extract data values from the
raw module data and produce an output parameter array. Currently there's one
Source implemented: The :ref:`Filter Extractor <analysis-extractor>`

.. _analysis-extractor:

Filter Extractor
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Filter Extractor uses a list of bit-level filters to classify input words
and extract address and data values.

Filter Basics
^^^^^^^^^^^^^
A single filter consists of 32 characters used to match a 32-bit data word. The
filter describes the static parts of the data used for matching and the
variable parts used for data extraction. The first character of a filter line
matches bit 31, the last character bit 0.

The following characters are used in filter strings:

+-----------+---------------------+
| Character | Description         |
+===========+=====================+
| ``0``     | bit must be cleared |
+-----------+---------------------+
| ``1``     | bit must be set     |
+-----------+---------------------+
| ``A``     | address bit         |
+-----------+---------------------+
| ``D``     | data bit            |
+-----------+---------------------+
| others    | don't care          |
+-----------+---------------------+

The following conventions are used in the default filters that come with mvme:

* ``X`` is used if any bit value is allowed.
* ``O`` (the letter) is used to denote the position of the *overflow* bit.
* ``U`` is used to denote the position of the *underflow* bit.
* ``P`` is used to denote the position of the *pileup* bit.

These characters are merely used to make it easier to identify certain bits
when editing a filter. With regards to matching any character other than ``0``
or ``1`` means that any bit value is allowed.

Any characters other than ``0`` and ``1`` mean that any
bit value is allowed.

.. highlight:: none

**Example**: The default *Amplitude* filter for the MDPP-16_SCP: ::

  0001 XXXX PO00 AAAA DDDD DDDD DDDD DDDD

The filter above contains a 4-bit address and a 16-bit data value. The
positions of the pileup and overflow bits are marked using ``P`` and ``O`` to
allow easily adjusting the filter to match for example non-overflow data only.

The number of address bits (``A``) determine the size of the Filter Extractors
output array.

Data extraction from an input data word is done by keeping only the bits
matching the address or data mask and then right shifting to align with the 0
bit.

.. note::
   The filter implementation assumes that address and data bits form
   consecutive sequences.
..
   When extracting values the code looks at the first and last occurence of the
   respective character in the filter line and treats the resulting sequence as
   if it consisted of only that character: ``A0AA`` will produce a 4-bit
   address value with bit 2 always being 0.

Each filter has an optional *word index* attached to it. If the word index is
set to a value >= 0, then the filter can only produce a match on the module
data word with the same index.

Multiple filter words
^^^^^^^^^^^^^^^^^^^^^

The Filter Extractor implementation allows combining multiple 32-bit filters to
match and extract data from multiple input words.

Filters are tried in order. If a previously unmatched filter produces a match
no further filters will be tried for the same data word.

Once all individual filters have been matched the whole combined filter matches
and address and data values can be extracted.

When extracting values the filters are again used in order: the first filter
produces the lowest bits of the combined result, the result of the next filter
is left-shifted by the amount of bits in the previous filter and so on.

.. note::
   The maximum number of bits that can be extracted for address and data values
   is limited to 64!

Matching and data extraction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

During a DAQ run or a replay the Filter Extractor gets passed all the data that
was produced by a single module readout (*Event Data*). Each data word is
passed to the internal filter.

Once the filter has completed *Required Completion Count* times address and
data values will be extracted.

The data value is cast to a double and a uniform random value in the range
``[0, 1)`` is added. This resulting value is stored in the output parameter
array at the index specified by the extracted address value.

User Interface
^^^^^^^^^^^^^^
In the Analysis UI right-click a Module and select *New -> Filter Extractor* to
add a new filter.

.. autofigure:: images/analysis_add_filter_extractor.png
   :scale-latex: 60%

   Filter Extractor UI

You can load predefined filters into the UI using the *Filter template* combo
box and the *Load Template into UI* button. This will replace the current
filter with the one from the template.

Use the *+* and *-* symbols to add/remove filter words. The spinbox right of
the filter string lets you specify a word index for the corresponding filter.

*Required Completion Count* allows you to specify how many times the filter has
to match before it produces data. This completion count starts from 0 on every
module event and is incremented by one each time the filter matches.

If *Generate Histograms* is checked raw and calibrated histograms will be
created for the filter. *Unit Label*, *Unit Min* and *Unit Max* are parameters
for the :ref:`Calibration Operator <analysis-calibration>`.


.. _analysis-operators:

Operators
----------------------------------------

mvme currently implements the following operators:


.. _analysis-Calibration:

Calibration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The calibration operator allows to add a unit label to a parameter array and to
calibrate input parameters using *unit min* and *unit max* values.

Each input parameters ``[lowerLimit, upperLimit)`` interval is mapped to the
outputs ``[unitMin, unitMax)`` interval.

.. aafig::
    :textual:
    :scale: 120

         +---------------------------+
         |Calibration                |
         +===========================+
         |"Out[i] = calibrate(In[i])"|
    ---->|"Out.Unit = Unit"          | ---->
         +---------------------------+

With *calibrate()*: ::

  Out = (In - lowerLimit) * (unitMax - unitMin) / (upperLimit - lowerLimit) + unitMin

.. note::
    Calibration information can also be accessed from adjacent 1D histograms.
    Refer to :ref:`analysis-histo1dsink` for details.


.. _analysis-IndexSelector:

Index Selector
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Select a specific index from the input array and copy it to the output.

This operator produces an output array of size 1.

.. aafig::
    :textual:
    :scale: 120

         +----------------------+
         |Index Selector        |
         +======================+
    ---->|"Out[0] = In[index]"  | ---->
         +----------------------+

.. _analysis-PreviousValue:

Previous Value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Outputs the input value from the previous event. Optionally outputs the last
input that was valid.

.. FIXME: Proper explanation here

Combine with the difference operator to calculate the distribution of change of a parameter.

.. aafig::
    :textual:
    :scale: 120

         +----------------------+
         |Previous Value        |
         +======================+
         |"Out[i] = Prev[i]"    |
    ---->|"Prev[i] = In[i]"     | ---->
         +----------------------+

.. _analysis-Difference:

Difference
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Produces the element-wise difference of its two inputs *A* and *B*: ::

  Output[i] = A[i] - B[i]

.. aafig::
    :textual:
    :scale: 120

          +----------------------+
          |Difference            |
      A   +======================+
    ----->|                      |
     "-B" |"Out[i] = A[i] - B[i]"| ---->
    ----->|                      |
          +----------------------+


.. _analysis-Sum:

Sum / Mean
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Calculates the sum (optionally the mean) of the elements of its input array.

This operator produces an output array of size 1.

.. aafig::
    :textual:
    :scale: 120

         +------------------------+
         |"Sum / Mean"            |
         +========================+
         |"Out[0] = Sum(In[0..N])"|
    ---->|                        | ---->
         +------------------------+

.. _analysis-ArrayMap:

Array Map
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Allows selecting and reordering arbitrary indices from a variable number of
input arrays.

.. aafig::
    :textual:
    :scale: 120
    :proportional:

    It's so very fugly!
             +------------------------+
             |Array Map               |
             +========================+
    "In#0"---->|                        |
    "In#0"---->|                        | ---->
    "In#0"---->|                        |
    "In#0"---->|                        |

.. autofigure:: images/analysis_array_map.png
   :scale-latex: 60%

   Array Map UI

* Use the *+* and *-* buttons to add/remove inputs.
* Select elements in the *Input* and *Output* lists and use the arrows to move
  them from one side to the other.

Multiple items can be selected by control-clicking, ranges of items by
shift-clicking. Both methods can be combined to select ranges with holes
in-between them. Focus a list and press ``Ctrl-A`` to select all items.

.. _analysis-RangeFilter1D:

1D Range Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Keeps values if they fall inside (optionally outside) a given interval. Input
values that do not match the criteria are set to *invalid* in the output.

.. _analysis-RectFilter2D:

2D Rectangle Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Produces a single *valid* output value if both inputs satisfy an interval based
condition.

.. _analysis-ConditionFilter:

Condition Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Copies data input to output if the corresponding element of the condition input
is valid.

.. _analysis-sinks:

Histograms (Sinks)
----------------------------------------

mvme currently implements the following data sinks:

.. _analysis-histo1dsink:

1D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. autofigure:: images/analysis_histo1d_listwidget.png
   :scale-latex: 60%

   1D Histogram List Widget

* Histograms and Histogram Lists
* 2D combined view of histogram lists!
* Zoom via drawing a rectangle. Zooms only in X
* Zoom out via right-click. Keeps a zoomstack
* UI: Gauss
* UI: Rate Est. (Refer to a "recipe" section where this is described)
* UI: Clear
* UI: Export
* UI: Save
* UI: Subrange
* UI: Resolution
* UI: Calibration
* UI: Info
* UI: Histogram #

.. _analysis-histo2dsink:

2D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. autofigure:: images/analysis_histo2d_widget.png
   :scale-latex: 60%

   2D Histogram Widget

* UI: X-Proj
* UI: Y-Proj
* UI: Clear
* UI: Export
* UI: Subrange
* UI: Resolution
* UI: Info


Importing Objects
----------------------------------------


.. * :ref:`analysis-Calibration`
..
..   * Calibrate values using a desired minimum and maximum.
..   * Add a unit label.
..
.. * :ref:`analysis-IndexSelector`
..
..   * Select a specific index from the input array and copy it to the output.
..
..   Produces an output array of size 1.
..
.. * :ref:`analysis-PreviousValue`
..
..   Outputs the input value from the previous event. Optionally outputs the last
..   input that was valid.
..
.. * :ref:`analysis-Difference`
..
..   Produces the element-wise difference of its two inputs.
..
.. * :ref:`analysis-Sum`
..
..   Calculates the sum (optionally the mean) of the elements of its input array.
..
..   Produces an output array of size 1.
..
.. * :ref:`analysis-ArrayMap`
..
..   Allows selecting and reordering arbitrary indices from a variable number of
..   input arrays.
..
..
.. * :ref:`analysis-RangeFilter1D`
..
..   Keeps values if they fall inside (optionally outside) a given interval. Input
..   values that do not match the criteria are set to *invalid* in the output.
..
..
.. * :ref:`analysis-RectFilter2D`
..
..   Produces a single *valid* output value if both inputs satisfy an interval
..   based condition.
..
.. * :ref:`analysis-ConditionFilter`
..
..   Copies data input to output if the corresponding element of the condition
..   input is valid.
