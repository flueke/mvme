cmake_minimum_required(VERSION 3.0.0)
project(mvme-a2)

#option(BUILD_TESTS "Build test binaries" ON)
#
#if (BUILD_TESTS)
#    enable_testing()
#endif()

set(WARNING_FLAGS "-Wall -Wno-unused-variable -Wno-unused-function")

#set(CMAKE_CXX_FLAGS "-std=c++14 -pipe ${WARNING_FLAGS}")
#
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mbmi2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
##set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
#
#set(MARCH_FLAG "nocona")
#set(MTUNE_FLAG "core2")
##set(MARCH_FLAG "skylake")
##set(MTUNE_FLAG "skylake")
##set(MARCH_FLAG "native")
##set(MTUNE_FLAG "native")
#
#set(CMAKE_CXX_FLAGS_DEBUG   "-g -Og")
#set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG}")
#set(CMAKE_CXX_FLAGS_PROFILE "-DNDEBUG -O3 -march=${MARCH_FLAG} -mtune=${MTUNE_FLAG} -pg -fno-omit-frame-pointer")

# Explicitly set the required c++ standard (CMake >= 3.1)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Create binaries in the root of the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(pcg-cpp-0.98/include/)

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
message("-- Found boost for liba2 in ${Boost_INCLUDE_DIRS}")

find_package(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})
message("-- Found zlib for liba2 in ${ZLIB_INCLUDE_DIRS}")

set(liba2_SOURCES
    a2.cc
    a2_exprtk.cc
    data_filter.cc
    listfilter.cc)

# Pass -mbig-obj to mingw gas on Win64. This works around the "too many
# sections" error when linking code with lots of templates.
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_source_files_properties(${liba2_SOURCES} PROPERTIES
        COMPILE_FLAGS "-Wa,-mbig-obj"
    )
endif()

add_library(liba2_shared SHARED ${liba2_SOURCES})
target_link_libraries(liba2_shared ${ZLIB_LIBRARIES})
set_target_properties(liba2_shared PROPERTIES OUTPUT_NAME a2)
target_include_directories(liba2_shared PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
include(GenerateExportHeader)
generate_export_header(liba2_shared
    EXPORT_MACRO_NAME "LIBA2_EXPORT"
    EXPORT_FILE_NAME "a2_export.h")

add_library(liba2_static STATIC ${liba2_SOURCES})
target_link_libraries(liba2_static ${ZLIB_LIBRARIES})
set_target_properties(liba2_static PROPERTIES
    OUTPUT_NAME a2
    POSITION_INDEPENDENT_CODE ON)

function(add_a2_test exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    add_dependencies(${exe_name} liba2_shared)
    target_link_libraries(${exe_name} liba2_shared)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_a2_test)

if(BUILD_TESTS)

    find_library(GOOGLE_BENCHMARK benchmark)
    find_path(GOOGLE_BENCHMARK_INCLUDE_DIR "benchmark/benchmark.h")
    include_directories(${GOOGLE_BENCHMARK_INCLUDE_DIR})
    find_package(Threads)

    function(add_a2_bench exe_name exe_sources)
        add_executable(${exe_name} ${exe_sources})
        add_dependencies(${exe_name} liba2_shared)
        if (WIN32)
            target_link_libraries(${exe_name} liba2_shared ${GOOGLE_BENCHMARK} Threads::Threads shlwapi.lib)
        else(WIN32)
            target_link_libraries(${exe_name} liba2_shared ${GOOGLE_BENCHMARK} Threads::Threads)
        endif(WIN32)
        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_a2_bench)

    add_a2_bench(test_a2_operators test_operators.cc)
    add_a2_bench(test_a2 test_a2.cc)
    add_a2_bench(test_a2_param test_a2_param.cc)
    add_a2_bench(test_a2_listfilter test_listfilter.cc)
    add_a2_bench(test_a2_arena test_arena.cc)

    add_executable(test_a2_exprtk test_a2_exprtk.cc)
    add_dependencies(test_a2_exprtk liba2_shared)
    target_link_libraries(test_a2_exprtk liba2_shared gtest gtest_main)
    add_test(NAME test_a2_exprtk COMMAND $<TARGET_FILE:test_a2_exprtk>)

endif(BUILD_TESTS)

file(COPY env DESTINATION ${CMAKE_BINARY_DIR})
#file(COPY histoviewer.py DESTINATION .)

# vim:tw=0
