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


==================================================
Operation Details
==================================================

Generation of readout stacks
----------------------------

    RDO module 0
    EndMarker
    RDO module 1
    EndMarker
    ..

The EndMarker is a unique pattern that is not produced by any of the VME
modules. Currently the value ``0x87654321`` is used.




.. include:: vme_script.rstinc
.. include:: analysis.rstinc
