##################################################
Reference
##################################################

.. warning::
    INCOMPLETE

* Analysis does not throttle DAQ! If the analysis part is too slow data buffers
  are not passed on to the analysis ("dropped" buffers).

  Data buffers are *all* written to disk. If disk writing is slow (possibly
  due to strong compression or just a low disk write performance) it can be the
  determining factor of mvmes performance. Usually the VME data is much lower
  than the write rate of modern harddisk or SSD drives.

* When replaying data from a listfile *all* data buffers are passed on to the
  analysis. In this case the analysis can be the major factor determining the
  performance of mvme.

.. include:: architecture.rstinc
.. include:: vme_script.rstinc
.. include:: analysis.rstinc
