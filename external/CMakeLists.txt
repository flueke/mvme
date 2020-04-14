# ftdi d3xx drivers
add_subdirectory(ftdi-d3xx)

# minizip options
option(MZ_COMPAT "Enables compatibility layer" OFF)
option(MZ_ZLIB "Enables ZLIB compression" ON)
option(MZ_BZIP2 "Enables BZIP2 compression" OFF)
option(MZ_LZMA "Enables LZMA compression" OFF)
option(MZ_PKCRYPT "Enables PKWARE traditional encryption" OFF)
option(MZ_WZAES "Enables WinZIP AES encryption" OFF)
option(MZ_LIBCOMP "Enables Apple compression" OFF)
option(MZ_OPENSSL "Enables OpenSSL for encryption" OFF)
option(MZ_BRG "Enables Brian Gladman's encryption library" OFF)
option(MZ_SIGNING "Enables zip signing support" OFF)
option(MZ_COMPRESS_ONLY "Only support compression" OFF)
option(MZ_DECOMPRESS_ONLY "Only support decompression" OFF)
option(MZ_BUILD_TEST "Builds minizip test executable" OFF)
option(MZ_BUILD_UNIT_TEST "Builds minizip unit test project" OFF)
option(MZ_BUILD_FUZZ_TEST "Builds minizip fuzzer executables" OFF)
option(MZ_CODE_COVERAGE "Builds with code coverage flags" OFF)
set(SKIP_INSTALL_ALL ON)
#set(ZLIB_REPOSITORY "https://github.com/Dead2/zlib-ng" CACHE STRING "")
#set(ZLIB_TAG "develop" CACHE STRING "")

add_subdirectory(minizip)
target_include_directories(minizip
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/minizip>)

# pybind11
if (MVLC_ENABLE_PYTHON)
    add_subdirectory(pybind11-2.4.3)
endif(MVLC_ENABLE_PYTHON)

# yaml-cpp
option(YAML_CPP_BUILD_TESTS "Enable testing" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
option(YAML_CPP_INSTALL "Enable generation of install target" OFF)
option(YAML_BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
add_subdirectory(yaml-cpp)
# Disable the 'yaml-cpp/node/impl.h:202:11: warning: ‘operator=’ should return # a reference to ‘*this’ [-Weffc++]' warning
# Note: the code does return *this.
set(not-msvc $<NOT:$<CXX_COMPILER_ID:MSVC>>)
target_compile_options(yaml-cpp
  PRIVATE $<${not-msvc}:-Wno-effc++>)

# Enable the PIC flag to fix a relocation error when linking against the
# static version of yaml-cpp.
set_target_properties(yaml-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# LZ4
add_subdirectory(lz4/contrib/cmake_unofficial)
target_include_directories(lz4_static
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lz4/lib/>)

# fmt
add_subdirectory(fmt)
