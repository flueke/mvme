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

# pybind11
if (MVLC_ENABLE_PYTHON)
    add_subdirectory(pybind11-2.4.3)
endif(MVLC_ENABLE_PYTHON)
