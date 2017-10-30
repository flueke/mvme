# https://root.cern.ch/how/integrate-root-my-project-cmake

list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

find_package(
    ROOT REQUIRED
    COMPONENTS Core RIO Tree Hist)

include(${ROOT_USE_FILE})

set(root_test_LIBRARIES
    ${ROOT_Core_LIBRARY}
    ${ROOT_RIO_LIBRARY}
    ${ROOT_Tree_LIBRARY}
    ${ROOT_Hist_LIBRARY}
    )

add_executable(root_test root_test.cc)
target_link_libraries(root_test ${root_test_LIBRARIES})
