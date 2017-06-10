.. mvme documentation master file, created by
   sphinx-quickstart on Fri Jun  9 14:45:22 2017.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. Note (flueke): Helpful links regarding Sphinx markup
.. http://www.sphinx-doc.org/en/stable/markup/misc.html#index-generating-markup

Welcome to mvme's documentation!
================================

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   intro
   quickstart
   architecture
..   analysis
..   vme_script


.. index:: ! IndexPage

Introduction
-------------------------------------------------
Intro text goes here
Testing a link here: :any:`architecture`

Features
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
* F1
* F2
* F42!

Quickstart
-------------------------------------------------
* Create event, select irq 1, vector 0.
* Create module, edit module interface settings. Change irq to 1.
* Edit module settings, enable pulser for testing
* In the analysis window right click the module and select 'generate default filters'
* In the main window press start to enter DAQ mode.
* Check the log for any errors that might have occured during initialization
* Double click the amplitude histograms to verify the pulser is working and
  data is being received properly.

Another Section that should get a nice tag
-------------------------------------------------


.. Indices and tables
.. ==================
.. 
.. * :ref:`genindex`
.. * :ref:`modindex`
.. * :ref:`search`
