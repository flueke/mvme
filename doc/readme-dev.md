# mvme developer information

## Compiling

* cmake
* qt >= 5.6 (?)
* qwt >= 6.1
* quazip
* c++14 capable compiler (gcc, clang)

### Windows

I'm currently building both the x32 and x64 windows releases in MSYS2
environments.


## Miscellaneous

### Clang Asan options
`export ASAN_OPTIONS="detect_leaks=false"`

### Can be used for changelog creation
`git log --no-merges --pretty="format:%aD, %an, * %s [%an - %h] %b"`
