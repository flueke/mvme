##################################################
Reference
##################################################

.. toctree::
   :maxdepth: 2
   :caption: Reference Contents:

   vme_script
   analysis


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




.. .. include:: vme_script.rstinc
.. .. include:: analysis.rstinc
