##################################################
Reference
##################################################

.. toctree::
    :maxdepth: 2
    :caption: Reference Contents:

    daq_and_replay
    vme_config
    vme_script
    analysis

.. _reference-json-rpc:

JSON-RPC remote control support
========================================

.. _jsonrpc: http://www.jsonrpc.org/specification

MVME uses the :ref:`JSON-RPC<jsonrpc>` specification to implement basic remote
control functionality. To enable/disable the RPC-Server use the "Workspace
Settings" button in the DAQ Controls Window and follow the instructions there.

If the RPC-Server is enabled mvme will open a TCP listening socket and accept
incoming connections. Requests are currently not length-prefixed, instead data
is read until a full JSON object has been received. The received JSON is then
interpreted according to the JSON-RPC spec.

**Note:** The JSON-RPC batch feature is not supported by the server implementation.

getVersion
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the version of MVME running the JSON RPC services.

* Parameters

  None

* Returns

  **String** containing version information.


getLogMessages
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the messages buffered up in the MVME log.

* Parameters

  None

* Returns

  **StringList** - List containing the log messages from oldest to newest.


getDAQStats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns information about the current DAQ run including counter values and
VME controller statistics.

* Parameters

  None

* Returns

  **Object**


getVMEControllerType
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the type of VME controller in use.

* Parameters

  None

* Returns

  **String**


getVMEControllerType
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns detailed VME controller statistics if available for the current
controller type.

* Parameters

  None

* Returns:

  **Object**




getDAQState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the current state of the DAQ.

* Parameters

  None

* Returns
  String - "Idle", "Starting", "Running", "Stopping" or "Paused"


startDAQ
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Starts a new DAQ run. The system must be idle, meaning any previous DAQ runs
must have been stopped.

* Parameters

  None

* Returns

  **true** on success, error status and additional information otherwise.


stopDAQ
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Stops the current DAQ run.

* Parameters

  None

* Returns

  **true** on success, error status and additional information otherwise.

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

