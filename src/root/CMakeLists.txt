# https://root.cern.ch/how/integrate-root-my-project-cmake

list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

find_package(
    ROOT
    COMPONENTS Core RIO Tree Hist)

if (ROOT_FOUND)
    include(${ROOT_USE_FILE})

    find_program(ROOT_CONFIG_EXECUTABLE root-config
        HINTS $ENV{ROOTSYS}/bin)

    execute_process(
        COMMAND ${ROOT_CONFIG_EXECUTABLE} --prefix
        OUTPUT_VARIABLE ROOTSYS
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    message("-- Found ROOT in ROOTSYS=${ROOTSYS}")

    set(root_test_LIBRARIES
        ${ROOT_Core_LIBRARY}
        ${ROOT_RIO_LIBRARY}
        ${ROOT_Tree_LIBRARY}
        ${ROOT_Hist_LIBRARY}
        )

    add_executable(root_test root_test.cc)
    target_link_libraries(root_test ${root_test_LIBRARIES})
endif(ROOT_FOUND)
