# mesytec-mvlc

*User space driver library for the Mesytec MVLC VME controller.*

## Components

* USB and ETH implementations DONE
  - Two pipes
  - Buffered reads
  - Unbuffered/low level reads
  - Max write size checks (eth packet size limit)?
  - support: error codes and conditions
  - Counters

* Dialog layer DONE
  - (UDP) retries
* Error polling
* Listfile format, Writer and Reader code
* Readout loop, Readout Worker
* Stack Building DONE
* Stack Management
* Readout/Response Parser using readout stack to parse incoming data
* listfile format, writer and reader, tools to get the readout config back to
  construct a readout parser for the file.
  mvme will thus be able to replay files record by the library.
  If mvme would also store the library generated config the other way would
  also work. Do this!

single create readout config:
  list of stack triggers
  list of readout stacks

single create readout instance:
  readout config
  readout buffer structure with crateId, number, type, capacity, used and view/read/write access
  readout buffer queue plus operations (blocking, non-blocking)
  listfile output (the readout config is serialized into the listfile at the start)

listfile writer
  should be able to take buffers from multiple readout workers
  takes copies of readout buffers and internally queues them up for writing
  listfile output
