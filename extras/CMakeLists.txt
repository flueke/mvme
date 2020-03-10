option(MVLC_ENABLE_CLI "Build the mvlc-cli demo binary" OFF)

if (MVLC_ENABLE_CLI)
    add_subdirectory(mvlc-cli)
endif(MVLC_ENABLE_CLI)

option(MVLC_ENABLE_PYTHON "Build the python wrapper module for mesytec-mvlc" OFF)

if (MVLC_ENABLE_PYTHON)
    add_subdirectory(python)
endif(MVLC_ENABLE_PYTHON)
