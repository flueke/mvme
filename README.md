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
