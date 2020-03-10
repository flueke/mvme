cmake_minimum_required(VERSION 3.10)

project(mesytec-mvlc
    DESCRIPTION "User space library for the Mesytec MVLC VME controller")

# Check if mesytec-mvlc is being used directly or via add_subdirectory
set(MESYTEC_MVLC_MASTER_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(MESYTEC_MVLC_MASTER_PROJECT ON)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#set(CMAKE_SKIP_BUILD_RPATH FALSE)
#set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
#set(BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (WIN32)
## Create binaries in the root of the build directory
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif(WIN32)

option(MVLC_BUILD_TESTS "Build test binaries" ${MESYTEC_MVLC_MASTER_PROJECT})

if (MVLC_BUILD_TESTS)
    # googletest / GTest
    # https://github.com/google/googletest/blob/master/googletest/README.md
    # Download and unpack googletest at configure time
    configure_file(cmake/googletest_CMakeLists.txt.in googletest-download/CMakeLists.txt)
    execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
    if(result)
      message(FATAL_ERROR "CMake step for googletest failed: ${result}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build .
      RESULT_VARIABLE result
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
    if(result)
      message(FATAL_ERROR "Build step for googletest failed: ${result}")
    endif()

    # Prevent overriding the parent project's compiler/linker
    # settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # Add googletest directly to our build. This defines
    # the gtest and gtest_main targets.
    add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
                     ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
                     EXCLUDE_FROM_ALL)

    # The gtest/gtest_main targets carry header search path
    # dependencies automatically when using CMake 2.8.11 or
    # later. Otherwise we have to add them here ourselves.
    if (CMAKE_VERSION VERSION_LESS 2.8.11)
      include_directories("${gtest_SOURCE_DIR}/include")
    endif()

    message("-- Building tests")
    include(CTest)
    enable_testing()
endif(MVLC_BUILD_TESTS)

add_subdirectory(external)
add_subdirectory(extras)
add_subdirectory(src)
