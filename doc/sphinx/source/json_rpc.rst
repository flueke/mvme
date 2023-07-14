.. index:: JSON-RPC
.. _reference-json-rpc:

JSON-RPC remote control support
========================================

.. _jsonrpc: http://www.jsonrpc.org/specification

MVME uses the `jsonrpc`_ specification to implement basic remote control
functionality. To enable/disable the RPC-Server use the "Workspace Settings"
button in the DAQ Controls Window and follow the instructions there.

If the RPC-Server is enabled mvme will open a TCP listening socket and accept
incoming connections. By default mvme binds to all interfaces and listens on
port 13800.

Requests are currently not length-prefixed, instead data is read until a full
JSON object has been received. The received JSON is then interpreted according
to the JSON-RPC spec.

**Note:** The JSON-RPC batch feature is not supported by the server implementation.

A command line client written in Python3 can be found in the mvme distribution
under ``extras/mvme_jsonrpc_client.py``: ::

    $ python3 extras/mvme_jsonrpc_client.py localhost 13800 getDAQState
    ---> {"id": "0", "jsonrpc": "2.0", "method": "getDAQState", "params": []}
    <--- {"id": "0", "jsonrpc": "2.0", "result": "Running"}

Examples
-----------------------------------------
* Requesting DAQ State: ::

    ---> {"id": "0", "jsonrpc": "2.0", "method": "getDAQState", "params": []}
    <--- {"id": "0", "jsonrpc": "2.0", "result": "Running"}

* Starting data acquisition: ::

    ---> {"id": "0", "jsonrpc": "2.0", "method": "startDAQ", "params": []}
    <--- {"id": "0", "jsonrpc": "2.0", "result": true}

* An error response: ::

    ---> {"id": "0", "jsonrpc": "2.0", "method": "startDAQ", "params": []}
    <--- {"error": {"code": 102, "message": "DAQ readout worker busy"}, "id": "0", "jsonrpc": "2.0"}

* Requesting DAQ stats: ::

    ---> {"id": "0", "jsonrpc": "2.0", "method": "getDAQStats", "params": []}
    <--- {"id": "0", "jsonrpc": "2.0", "result": {"analysisEfficiency": 1, "analyzedBuffers": 4644, "buffersWithErrors": 0, "currentTime": "2018-06-14T11:45:21", "droppedBuffers": 0, "endTime": null, "listFileBytesWritten": 0, "listFileFilename": "", "runId": "180614_114412", "startTime": "2018-06-14T11:44:13", "state": "Running", "totalBuffersRead": 4644, "totalBytesRead": 6366924, "totalNetBytesRead": 5851300}}

Methods
-----------------------------------------

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
Returns information about the current DAQ run including counter values, the
current run ID, listfile output filename and start time.

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


getVMEControllerStats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns detailed VME controller statistics if available for the current
controller type.

* Parameters

  None

* Returns:

  **Object**


getVMEControllerState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the connection state of the VME controller.

* Parameters

  None

* Returns:

  **String** - "Connected", "Disconnected" or "Connecting"

reconnectVMEController
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Starts a reconnection attempt of the VME controller. The operation is
asynchronous, thus the result will not be directly available. Instead the
controller state needs to be polled via ``getVMEControllerState`` to see the
result of the reconnection attempt.

**Note**: this might in the future be changed to a synchronous version, which
immediately returns the result or any errors that occured.

* Parameters

  None

* Returns:

  **String** - "Reconnection attempt initiated"

getSystemState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Combined system state for both the readout/replay side and the analysis/data
consumer side.

* Returns
  String - "Idle", "Starting", "Running", "Stopping" or "Paused"


getDAQState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the current state of the DAQ/replay side.

* Returns
  String - "Idle", "Starting", "Running", "Stopping" or "Paused"

getAnalysisState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the current state of the analysis/data consumer side.

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

getGlobalMode() -> string
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the global mode mvme is in. Either  "daq" or "replay".

loadAnalysis(string: filepath) -> bool
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Attempts to load a mvme analysis from the given file (\*.analysis).


loadListfile(string: filepath)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Attempts to open the specified file as a mvme listfile (\*.zip).

startReplay([dict: options])
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Attempts to replay from the currently open listfile.

* Parameters:

  - options: dictionary, default: { "keepHistoContents": true }

  Options for the analysis system. "keepHistoContents" allows to keep or clear
  the histogram contents from a previous run.