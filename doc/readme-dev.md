# mvme developer readme

## Compiling

* c++14 capable compiler (gcc, clang)
* cmake
* qt >= 5.6 (?)
* qwt >= 6.1
* quazip

    $ cmake -DCMAKE_BUILD_TYPE=Debug ../mvme2
    $ make -j4

## Archlinux packages:
`base-devel cmake qt5-base quazip libusb-compat qwt`

### CMake Options
* `BUILD_DOCS=ON|OFF`
  If enabled the doc subdirectory is included. CMake will look for Sphinx, if
  found the documentation will be built.

### Windows

I'm currently building both the x32 and x64 windows releases in MSYS2
environments.

Some of the dependencies required to compile in MSYS2:

    make 4.2.1-1
    mingw-w64-x86_64-cmake 3.7.2-2
    mingw-w64-x86_64-gcc 6.3.0-2
    mingw-w64-x86_64-pkg-config 0.29.1-3
    mingw-w64-x86_64-qt5 5.8.0-3
    mingw-w64-x86_64-quazip 0.7.1-1
    mingw-w64-x86_64-qwt-qt5 6.1.2-2
    mingw-w64-x86_64-zlib 1.2.11-1

The above list contains the x64 package names. The same packages need to be
installed in the x32 environment.

Optionally add `python2-pip` to install Sphinx if you want to generate
documentation. Additionally a latex system is required for PDF generation. I
successfully compiled the generated latex code using MiKTeX.

#### CMake invocation
* Debug:

  `cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=./install-prefix -DBUILD_DOCS=OFF -G"MSYS Makefiles" ../mvme2`


## Miscellaneous

### Clang Asan options
`export ASAN_OPTIONS="detect_leaks=false"`

### Can be used for changelog creation
`git log --no-merges --pretty="format:%aD, %an, * %s [%an - %h] %b"`
