add_subdirectory(ftdi-d3xx)

if (MVLC_ENABLE_PYTHON)
    add_subdirectory(pybind11-2.4.3)
endif(MVLC_ENABLE_PYTHON)
