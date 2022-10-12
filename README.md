# mvme - mesytec VME data acquisition

## Building mvme
### Dependencies
* gcc/clang with c++14 support
* cmake
* Qt >= 5.15
* qwt >= 6.2.0
* quazip
* libusb-0.1
* zlib
* graphviz
* boost
* ninja or make
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

#### Build commands on a fresh installation of Debian 10 (Buster)

Starting point is a clean installation of Debian Buster with the *XFCE* desktop
enviroment selected during installation time.

    apt-get install build-essential cmake libboost-all-dev qt5-default \
                    qtbase5-dev-tools libquazip5-dev libqwt-qt5-dev \
                    zlib1g-dev libusb-dev libqt5websockets5-dev ninja-build \
                    libgraphviz-dev
    git clone https://github.com/flueke/mvme
    cd mvme
    git submodule update --init
    mkdir build
    cd build
    cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=~/local/mvme ..
    ninja

### Windows MSYS2

http://www.msys2.org/

#### MSYS2 dependencies
* make
* mingw-w64-x86_64-cmake
* mingw-w64-x86_64-gcc
* mingw-w64-x86_64-pkg-config
* mingw-w64-x86_64-qt5
* mingw-w64-x86_64-quazip
* mingw-w64-x86_64-qwt-qt5
* mingw-w64-x86_64-zlib
* mingw-w64-x86_64-graphviz

#### CMake invocation under windows
`cmake -DCMAKE_BUILD_TYPE=Release -G"MSYS Makefiles" ../mvme`
`make -j4`

## Mac OS X using Homebrew
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
