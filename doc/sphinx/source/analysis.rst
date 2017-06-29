==================================================
Analysis
==================================================

Intro
----------------------------------------

Runtime Behaviour
----------------------------------------

Parameter Arrays
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Importing Objects
----------------------------------------


Data Sources
----------------------------------------
Analysis sources attach directly to a VME module. On every step of the analysis
system they're handed all the data words produced by that module in the
corresponding readout cycle. Their job is to extract data values from the raw
module data and produce an output parameter array. Currently there's one Source
implemented: The **Extractor**

Extractor
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The Extractor uses a list of bit-level filters to classify input words and
extract address and data values.

Operators
----------------------------------------

Calibration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
IndexSelector
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Previous Value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Difference
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Sum/Mean
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Array Map
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1D Range Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
2D Rectangle Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Condition Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sinks
----------------------------------------
1D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
2D Histogram
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
