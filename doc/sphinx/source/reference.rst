.. index:: Reference

##################################################
Reference
##################################################

.. toctree::
    :maxdepth: 2
    :caption: Reference Contents:

    daq_and_replay
    vme_config
    vme_script
    mvlc_trigger_io
    analysis
    json_rpc
    ref_event_server
    ref_metrics

.. ==================================================
.. Operation Details
.. ==================================================
..
.. Generation of readout stacks
.. ----------------------------
..
..     RDO module 0
..     EndMarker
..     RDO module 1
..     EndMarker
..     ..
..
.. The EndMarker is a unique pattern that is not produced by any of the VME
.. modules. Currently the value ``0x87654321`` is used.

.. vme_setup:
..   * Description of module scripts, global scripts. When are they run, what's
..     the base address
..   * Template system(?)
..   * Generation of readout stacks
.. vme_script: Script reference: syntax, commands, example
.. analysis:   internals, runtime behaviour, operator reference, importing of objects
.. listfile_format:
..   * Use the description from the source code.
..   * Refer to the listfile-dumper code that should be included in the installation.
..     TODO: Install this stuff!
