# Building mvme2
## Dependencies
* c++14 capable compiler
* Qt >= 5.7 (might work with earlier releases)
* libqwt
* libusb
* more???

## Linux
  `cmake -DCMAKE_BUILD_TYPE=Release ../mvme2`

## Windows MSYS2
  `cmake -DCMAKE_BUILD_TYPE=Release -G"MSYS Makefiles" ../mvme2`

# Libraries and 3rd-party code
* http://qwt.sourceforge.net/
* https://github.com/Elypson/qt-collapsible-section
* CVMUSBReadoutList from NSCLDAQ (http://docs.nscl.msu.edu/daq/,
  https://sourceforge.net/projects/nscldaq/)
