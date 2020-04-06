# ftdi d3xx drivers
add_subdirectory(ftdi-d3xx)

# libzip options
OPTION(ENABLE_COMMONCRYPTO "Enable use of CommonCrypto" OFF)
OPTION(ENABLE_GNUTLS "Enable use of GnuTLS" OFF)
OPTION(ENABLE_MBEDTLS "Enable use of mbed TLS" OFF)
OPTION(ENABLE_OPENSSL "Enable use of OpenSSL" OFF)
OPTION(ENABLE_WINDOWS_CRYPTO "Enable use of Windows cryptography libraries" OFF)

OPTION(ENABLE_BZIP2 "Enable use of BZip2" OFF)
OPTION(ENABLE_LZMA "Enable use of LZMA" OFF)

OPTION(BUILD_TOOLS "Build tools in the src directory (zipcmp, zipmerge, ziptool)" OFF)
OPTION(BUILD_REGRESS "Build regression tests" OFF)
OPTION(BUILD_EXAMPLES "Build examples" OFF)
OPTION(BUILD_DOC "Build documentation" OFF)

add_subdirectory(libzip)
target_include_directories(zip
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/libzip>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libzip/lib>)

# minizip options
option(MZ_COMPAT "Enables compatibility layer" ON)
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
#set(ZLIB_REPOSITORY "https://github.com/Dead2/zlib-ng" CACHE STRING "")
#set(ZLIB_TAG "develop" CACHE STRING "")

add_subdirectory(minizip)
target_include_directories(minizip
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/minizip>)

# pybind11
if (MVLC_ENABLE_PYTHON)
    add_subdirectory(pybind11-2.4.3)
endif(MVLC_ENABLE_PYTHON)

option(YAML_CPP_BUILD_TESTS "Enable testing" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
option(YAML_CPP_INSTALL "Enable generation of install target" OFF)
option(YAML_BUILD_SHARED_LIBS "Build Shared Libraries" ON)
add_subdirectory(yaml-cpp)
