# mvme developer readme

This file contains some notes and hints about working on mvme. For build
instructions refer to the top-level README.md file.

## Archlinux packages:
`cmake qt5-base quazip libusb-compat qwt`

### CMake Options
* `BUILD_DOCS=ON|OFF`
  If enabled the doc subdirectory is included. CMake will look for Sphinx, if
  found the documentation will be built.

### Windows

I'm currently building the x64 windows releases in a MSYS2 environment.

The x32 version cannot be built anymore due to linker memory restrictions (the
c++ template code of the exprtk library produces a lot of symbols which the x32
linker cannot seem to handle).

Some of the dependencies required to compile in MSYS2:

    make 4.2.1-1
    mingw-w64-x86_64-cmake
    mingw-w64-x86_64-gcc
    mingw-w64-x86_64-pkg-config
    mingw-w64-x86_64-qt5
    mingw-w64-x86_64-quazip
    mingw-w64-x86_64-qwt-qt5
    mingw-w64-x86_64-zlib

Optionally add `python2-pip` to install Sphinx if you want to generate
documentation. Additionally a latex system is required for PDF generation. I
successfully compiled the generated latex code using MiKTeX.

Additionally the libusb-win32 is required: https://sourceforge.net/projects/libusb-win32/

#### CMake invocation
* Debug:

  `cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=./install-prefix -DBUILD_DOCS=OFF -G"MSYS Makefiles" ../mvme2`


## Miscellaneous

### Clang/GCC Asan options
`export ASAN_OPTIONS="detect_leaks=false"`

Important:
To get line numbers in address sanitizer stack traces make sure libmvme.so can
be found in LD\_LIBRARY\_PATH. When running from the build directory do something like
`LD_LIBRARY_PATH=`pwd` gdb -ex r ./mvme`

I've also found these mentioned on some blog. They might be useful when using ASAN with gcc.
```
export ASAN_SYMBOLIZER=`which llvm-symbolizer`
export ASAN_OPTIONS="symbolize=1"
```

asan\_mvme.supp is a suppression file containing rules for libfontconfig and
libdbus-1. Usage:
```
  LSAN_OPTIONS=suppressions=../mvme2/asan_mvme.supp ./mvme
```


### Can be used for changelog creation
`git log --no-merges --pretty="format:%aD, %an, * %s [%an - %h] %b"`

## Profiling

Use `cmake -DCMAKE_BUILD_TYPE=Profile -DBUILD_DOCS=OFF  ../mvme2` to build.
This enables optimizations,  profiling (-pg) and keeps the frame pointer
around.

Install the correct perf package for your kernel, e.g. `apt-get install linux-perf-4.9`.

Run mvme using `perf record -g ./mvme`.

* Clearing filesystem caches on linux:
    `sync; echo 3 >| /proc/sys/vm/drop_caches`

### Reporting:
* `perf stat`
* `perf report -g`
* `perf report -g 'graph,0.5,caller'`

### Keeping the compiler from optimizing away code you want to benchmark

```
static void escape(void *p)
{
    asm volatile("" : : "g"(p) : "memory");
}

static void clobber()
{
    asm volatile("" : : : "memory");
}
```

## SIS3153
MAC Address: 00-00-56-15-3X-XX

With XXX being the serial number in hex.
`040 -> 0x028 -> 0-28`
`044 -> 0x02c -> 0-2c`

### Windows
`arp -s 192.168.178.44 00-00-56-15-30-2c`
`arp -s 192.168.178.44 00-00-56-15-30-28`

## Creating a source archive:

```
git archive -v  -o ~/src/mvme-packages/mvme-`git describe`-src.tar.gz --prefix=mvme-`git describe`-src/ dev
```

The "dev" at the very end is the branch name I wanted to package. It would be
nice to be able to just use the current branch/tip of tree.

# ROOT

## Building ROOT

* Packages required:

  dpkg-dev      (not sure if needed with the cmake method
  libxft-dev
  clang         (I guess)

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/florian/local/root-6.10.08/ ../root-6.10.08/
