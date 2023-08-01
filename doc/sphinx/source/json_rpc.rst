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
under ``extras/mvme_jsonrpc_client.py``. It allows to (repeatedly) call a single
RPC method: ::

    $ python3 extras/mvme_jsonrpc_client.py localhost 13800 getDAQState
    ---> {"id": "0", "jsonrpc": "2.0", "method": "getDAQState", "params": []}
    <--- {"id": "0", "jsonrpc": "2.0", "result": "Running"}

Since mvme-1.8.0 there is a second script ``extras/mvme_jsonrpc_replay.py``
which can replay data from multiple listfile archives.

Methods
-----------------------------------------

getVersion
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Returns the version of MVME running the JSON RPC services.

* Returns

  **String** containing version information.


getLogMessages
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the messages buffered up in the MVME log.

* Returns

  **StringList** - List containing the log messages from oldest to newest.


getDAQStats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns information about the current DAQ run including counter values, the
current run ID, listfile output filename and start time.

* Returns

  **Object**


getVMEControllerType
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the type of VME controller in use.

* Returns

  **String**


getVMEControllerStats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns detailed VME controller statistics if available for the current
controller type.

* Returns:

  **Object**


getVMEControllerState
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the connection state of the VME controller.

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

* Returns

  **true** on success, error status and additional information otherwise.


stopDAQ
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Stops the current DAQ run or replay.

* Returns

  **true** on success, error status and additional information otherwise.

getGlobalMode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Returns the global mode mvme is in. Either  "daq" or "replay".

* Returns

  string - "daq" or "replay"

loadAnalysis
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``loadAnalysis(string: filepath)``

Attempts to load a mvme analysis from the given file (\*.analysis).

loadListfile
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
``loadListfile(string: filepath, bool: loadAnalysis = false, bool replayAllParts = true)``

Attempts to open the specified file as a mvme listfile (\*.zip).

* Parameters

  - string: filepath - Path to the ZIP listfile. Absolute or relative to the
  current workspace directory.

  - bool: loadAnalysis = false - If true load the analysis contained in the
  listfile archive.

  - bool: replayAllParts = true - If a split listfile is detected (filename ending
  in 'partNNN') all parts starting from the given filename will be replayed.

* Returns

  **true** on success, error info object otherwise.

closeListfile
~~~~~~~~~~~~~
Closes the currently open listfile. The system goes into 'DAQ" state. Has no
effect if no listfile is open.

startReplay
~~~~~~~~~~~
``startReplay(bool: keepHistoContents = false)``

Attempts to replay from the currently open listfile.

* Parameters:

  - bool: keepHistoContents = false

    If keepHistoContents is true the analysis histograms will not be cleared
    before starting the replay.

stopReplay
~~~~~~~~~~
Stops the active listfile replay.

Examples
-----------------------------------------

Raw JSON data
~~~~~~~~~~~~~

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

mvme_jsonrpc_replay.py
~~~~~~~~~~~~~~~~~~~~~~

* Load analysis from ``listfiles/mvmelstrun004_part001.zip`` and replay data from
  the same archive only:

  ``mvme_jsonrpc_replay.py  listfiles/mvmelstrun004_part001.zip --loadAnalysis``

* Keep the currently opened analysis and accumulate data
  from ``listfiles/mvmelstrun004_part001.zip`` without clearing the histograms first.

  ``mvme_jsonrpc_replay.py  listfiles/mvmelstrun004_part001.zip --keepHistoContents``

* Replay all parts of a split run starting from ``part001``:

  ``mvme_jsonrpc_replay.py  listfiles/mvmelstrun004_part001.zip --replayAllParts``
