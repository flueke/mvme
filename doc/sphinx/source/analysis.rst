==================================================
Analysis
==================================================

System Structure
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
system is "stepped" by events: in each step all the :ref:`analysis-sources`
attached to a module get passed the modules event data. The task of each source
is to extract relevant values from its input data and make these values
available to subsequent operators and sinks.

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
  parameters value should fall into. This is used by histogram sinks to
  determine the parameters range and thus calculate the binning.

Input types
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Different operators have different requirements on their input types. The
:ref:`Calibration Operator <analysis-calibration>` for example can use whole
parameter arrays as its input, transforms each data value and produces an
output array of the same size as the input size.

Other operators can only act on individual values and thus connect directly to
a specific *index* into the parameter array. An example is the :ref:`2D
Histogram Sink <analysis-histo2dsink>`: it requires exactly two input values, X
and Y, neither of which can be a full array.

.. figure:: images/analysis_input_types.png

   Example of different input types

Each Operator implementation decides which types of input connections it
accepts. Some operators even change the type of inputs they accept based on the
first input type that is connected (they either accept full arrays for all
their inputs or single values for all their inputs).

The :ref:`Analysis UI <analysis-ui>` will highlight valid input nodes in green
when selecting an operators input.

Runtime Behaviour
----------------------------------------

.. _analysis-ui:

User Interface
----------------------------------------

Importing Objects
----------------------------------------

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
when editing a filter. Any characters other than ``0`` and ``1`` mean that any
bit value is allowed.

.. highlight:: none

**Example**: The default *Amplitude* filter for the MDPP-16_SCP: ::

  0001 XXXX PO00 AAAA DDDD DDDD DDDD DDDD

The filter contains a 4-bit address and a 16-bit data value. The positions of
the pileup and overflow bits are marked using ``P`` and ``O`` to allow easily
adjusting the filter to match for example non-overflow data only.

The number of address bits (``A``) determine the size of the Filter Extractors
output array.

.. FIXME: Better and more sane explanation here.

Data extraction from an input data word is done by keeping only the bits
matching the address or data mask and then right shifting to align with the 0
bit.

.. note::
   The filter implementation assumes that address and data bits form
   consecutive sequences.

   When extracting values the code looks at the first and last occurence of the
   respective character in the filter line and treats the resulting sequence as
   if it consisted of only that character: ``A0AA`` will produce a 4-bit
   address value with bit 2 always being 0.

Multiple filter words
^^^^^^^^^^^^^^^^^^^^^


User Interface
^^^^^^^^^^^^^^







.. _analysis-operators:

Operators
----------------------------------------

mvme currently implements the following operators:

* :ref:`analysis-Calibration`

  * Calibrate values using a desired minimum and maximum.
  * Add a unit label.

* :ref:`analysis-IndexSelector`

  * Select a specific index from the input array and copy it to the output.

  Produces an output array of size 1.

* :ref:`analysis-PreviousValue`

  Outputs the input value from the previous event. Optionally outputs the last
  input that was valid.

* :ref:`analysis-Difference`

  Produces the element-wise difference of its two inputs.

* :ref:`analysis-Sum`

  Calculates the sum (optionally the mean) of the elements of its input array.

  Produces an output array of size 1.

* :ref:`analysis-ArrayMap`

  Allows selecting and reordering arbitrary indices from a variable number of
  input arrays.


* :ref:`analysis-RangeFilter1D`

  Keeps values if they fall inside (optionally outside) a given interval. Input
  values that do not match the criteria are set to *invalid* in the output.


* :ref:`analysis-RectFilter2D`

  Produces a single *valid* output value if both inputs satisfy an interval
  based condition.

* :ref:`analysis-ConditionFilter`

  Copies data input to output if the corresponding element of the condition
  input is valid.


.. _analysis-Calibration:

Calibration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-IndexSelector:

Index Selector
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-PreviousValue:

Previous Value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-Difference:

Difference
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-Sum:

Sum/Mean
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-ArrayMap:

Array Map
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-RangeFilter1D:

1D Range Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-RectFilter2D:

2D Rectangle Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-ConditionFilter:

Condition Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-sinks:

Sinks
----------------------------------------

.. _analysis-histo1dsink:

1D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _analysis-histo2dsink:

2D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
