# mvme - mesytec VME data acquisition

## Building mvme
### Dependencies
* gcc/clang with c++17 support
* cmake
* Qt >= 5.15
* qwt >= 6.1.3
* quazip
* libusb-0.1
* zlib
* graphviz
* boost
* ninja or make
* Optional: zmq
* Optional: prometheus-cpp
* Optional: sphinx and latex for the documentation
* Optional: NSIS for the windows installer

### Linux

    git clone https://github.com/flueke/mvme
    cd mvme
    git submodule update --init
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/mvme ..
    make -j4
    make install

The install step is optional, mvme does run directly from the build directory.

#### Build commands for Debian:

See the files under extras/docker for up-to-date dependencies and build steps.

### Windows ucrt64 via msys2 (updated 4/2025)

http://www.msys2.org/

#### ucrt64 dependencies

    mingw-w64-ucrt-x86_64-clang
    mingw-w64-ucrt-x86_64-ninja
    mingw-w64-ucrt-x86_64-quazip-qt5
    mingw-w64-ucrt-x86_64-qt5-websockets
    mingw-w64-ucrt-x86_64-qt5-svg
    mingw-w64-ucrt-x86_64-boost
    mingw-w64-ucrt-x86_64-qwt-qt5
    mingw-w64-ucrt-x86_64-graphviz
    mingw-w64-ucrt-x86_64-quazip-qt5

#### ucrt64 build

    mkdir build; cd build
    cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ..
    ninja

## Mac OS X using Homebrew (out of date, needs verification)
* Note that the OS X port has only been tested with our MVLC VME Controller via
  Ethernet. USB support has not been tested. There are also some ugly fonts and
  broken layouts which might get fixed at some point.

* Install Homebrew from https://brew.sh/
* `brew install git cmake boost qt quazip qwt libusb-compat sphinx-doc`
* Add the following to `~/.bash_profile`:
    ```
    export PATH="/usr/local/opt/qt/bin:/usr/local/opt/sphinx-doc/bin:$PATH"
    export CMAKE_PREFIX_PATH="/usr/local"
    ```

* `source ~/.bash_profile`
* Checkout mvme, create a build directory and run cmake:
    ```
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=~/local/mvme ../mvme
    make -j6
    ```

* You should now be able to start mvme directly from the build directory or run
  `make install` and run from the installation directory.

## Libraries and 3rd-party code used in mvme
* http://qwt.sourceforge.net/
* https://github.com/Elypson/qt-collapsible-section
* CVMUSBReadoutList from NSCLDAQ (http://docs.nscl.msu.edu/daq/,
  https://sourceforge.net/projects/nscldaq/)
* https://github.com/preshing/cpp11-on-multicore by Jeff Preshing
