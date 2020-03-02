# mesytec-mvlc

*User space driver library for the Mesytec MVLC VME controller.*

## Components

* USB and ETH implementations
  - Two pipes
  - Buffered reads
  - Unbuffered/low level reads
  - Max write size checks (eth packet size limit)?
  - support: error codes and conditions
  - Counters

* Type erased handler, copyable (shares state), thread-safe, access to concrete
  implementation (typeinfo check and cast).

* Dialog layer 
* Error polling
* Readout loop
* Stack Building
* Stack Management
* Readout/Response Parser using readout stack to parse incoming data
