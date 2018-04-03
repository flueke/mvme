.. mvme documentation master file, created by
   sphinx-quickstart on Fri Jun  9 14:45:22 2017.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

.. Note (flueke): Helpful links regarding Sphinx markup
.. http://www.sphinx-doc.org/en/stable/markup/misc.html#index-generating-markup

.. TODO
.. - Describe the DAQ and Replay controls.
.. - Describe listfile controls, recording and replays
..   Also: VME Config is stored in the listfile
..   When using ZIP the current analysis config is also stored inside the
..     archive and can be extracted and loaded.

.. DAQ:
..   Start / Pause
..   Stop
..   1 Cycle
..
.. Replay:
..   Start / Pause
..   Stop
..   1 Event / Next Event
..
.. Controller:
..   Reconnect
..
.. Write Listfile
.. Write to ZIP
..   Compression

##################################################
mvme - mesytec VME Data Acquisition
##################################################

.. toctree::
   :maxdepth: 4
   :caption: Contents:

   intro
   installation
   quickstart
   reference
   howto
   changelog


.. only:: html

  Indices and tables
  ==================

  * :ref:`genindex`
  * :ref:`search`

.. * :ref:`modindex`

.. architecture

.. Generating keywords for the qthelp system:
.. .. index:: Something Wicked

.. .. index:: Pair and Couple
..    pair: foo; bar
..    single: coupling
