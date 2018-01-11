# mvme - mesytec VME data acquisition

## Building mvme2
### Dependencies
* c++14 capable compiler (gcc, clang)
* Qt >= 5.7
* qwt
* quazip
* libusb-0.1
* Optional: a recent version of libhdf5 to enable analysis session save/load
* Optional: sphinx and latex for the documentation
* Optional: NSIS for the windows installer
* Optional: ROOT (root export is work in progress)

### Linux
`cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_HDF5=On -DCMAKE_INSTALL_PREFIX=/usr/local ../mvme2`
`make -j4`

### Windows MSYS2

#### MSYS2 dependencies
* make
* mingw-w64-x86_64-cmake
* mingw-w64-x86_64-gcc
* mingw-w64-x86_64-pkg-config
* mingw-w64-x86_64-qt5
* mingw-w64-x86_64-quazip
* mingw-w64-x86_64-qwt-qt5
* mingw-w64-x86_64-zlib

#### CMake invocation under windows
`cmake -DCMAKE_BUILD_TYPE=Release -G"MSYS Makefiles" -DENALBE_HDF5=On ../mvme2`
`make -j4`

## Libraries and 3rd-party code used in mvme
* http://qwt.sourceforge.net/
* https://github.com/Elypson/qt-collapsible-section
* CVMUSBReadoutList from NSCLDAQ (http://docs.nscl.msu.edu/daq/,
  https://sourceforge.net/projects/nscldaq/)
* https://github.com/preshing/cpp11-on-multicore by Jeff Preshing
