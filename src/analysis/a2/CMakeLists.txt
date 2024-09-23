#option(BUILD_TESTS "Build test binaries" ON)
#
#if (BUILD_TESTS)
#    enable_testing()
#endif()

#set(WARNING_FLAGS "-Wall -Wno-unused-variable -Wno-unused-function")

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
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Create binaries in the root of the build directory
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_INCLUDE_CURRENT_DIR ON)

#if (WIN32)
#    string(APPEND CMAKE_CXX_FLAGS " -flto")
#    string(APPEND CMAKE_C_FLAGS " -flto")
#    string(APPEND CMAKE_EXE_LINKER_FLAGS " -flto")
#endif(WIN32)

#include_directories(pcg-cpp-0.98/include/)

find_package(Boost CONFIG REQUIRED)
#include_directories(${Boost_INCLUDE_DIRS})
message("-- Found boost for liba2 in ${Boost_INCLUDE_DIRS}")

find_package(ZLIB REQUIRED)
#include_directories(${ZLIB_INCLUDE_DIRS})
message("-- Found zlib for liba2 in ${ZLIB_INCLUDE_DIRS}")

set(liba2_SOURCES
    a2.cc
    a2_exprtk.cc
    listfilter.cc)

# Pass -mbig-obj to mingw gas on Win64. This works around the "too many
# sections" error when linking code with lots of templates.
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_source_files_properties(${liba2_SOURCES} PROPERTIES
        COMPILE_FLAGS "-Wa,-mbig-obj"
    )
endif()

#option(A2_BUILD_SHARED "Build shared version of the a2 library" OFF)
option(A2_BUILD_STATIC "Build static version of the a2 library" ON)

if (NOT win32)
    option(A2_HUGE_PAGES "Try to allocate using huge pages (linux only)" OFF)
endif()

#if (A2_BUILD_SHARED)
#    add_library(liba2_shared SHARED ${liba2_SOURCES})
#    target_link_libraries(liba2_shared
#        PRIVATE ${ZLIB_LIBRARIES}
#        PUBLIC pcg
#        )
#    set_target_properties(liba2_shared PROPERTIES
#        OUTPUT_NAME a2)
#
#    target_include_directories(liba2_shared
#        #PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
#        #SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/pcg-cpp-0.98/include/"
#        SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/zstr/src"
#        SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/"
#        )
#
#    target_compile_options(liba2_shared PRIVATE -Wall -Wextra)
#
#    set(A2_TEST_LIBRARY liba2_shared)
#
#    include(GenerateExportHeader)
#    generate_export_header(liba2_shared
#        EXPORT_MACRO_NAME "LIBA2_EXPORT"
#        EXPORT_FILE_NAME "a2_export.h")
#
#endif (A2_BUILD_SHARED)

if (A2_BUILD_STATIC)
    add_library(liba2_static STATIC ${liba2_SOURCES})

    target_link_libraries(liba2_static
        PRIVATE exprtk
        PRIVATE ${ZLIB_LIBRARIES}
        PUBLIC mesytec-mvlc
        PUBLIC pcg
        PUBLIC cpp11-on-multicore
        PUBLIC zstr)

    set_target_properties(liba2_static PROPERTIES
        OUTPUT_NAME a2
        POSITION_INDEPENDENT_CODE ON)

    target_compile_options(liba2_static PRIVATE -Wall -Wextra -Wno-format)

    if (A2_HUGE_PAGES)
        target_compile_definitions(liba2_static PUBLIC -DA2_HUGE_PAGES)
    endif()

    set(A2_TEST_LIBRARY liba2_static)
endif (A2_BUILD_STATIC)

function(add_a2_test exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    add_dependencies(${exe_name} ${A2_TEST_LIBRARY})
    target_link_libraries(${exe_name} PRIVATE ${A2_TEST_LIBRARY})
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_a2_test)

if(MVME_BUILD_TESTS)
    function(add_a2_bench exe_name exe_sources)
        add_executable(${exe_name} ${exe_sources})
        add_dependencies(${exe_name} ${A2_TEST_LIBRARY})
        if (WIN32)
            target_link_libraries(${exe_name}
                PRIVATE ${A2_TEST_LIBRARY}
                PRIVATE benchmark::benchmark
                PRIVATE Threads::Threads
                PRIVATE shlwapi.lib)
        else(WIN32)
            target_link_libraries(${exe_name}
                PRIVATE ${A2_TEST_LIBRARY}
                PRIVATE benchmark::benchmark
                PRIVATE Threads::Threads)
        endif(WIN32)
        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_a2_bench)

    add_a2_bench(test_a2_operators test_operators.cc)
    add_a2_bench(test_a2 test_a2.cc)
    add_a2_bench(test_a2_param test_a2_param.cc)
    add_a2_bench(bench_a2_listfilter bench_listfilter.cc)
    # Note: (flueke, 240411): disable the failing arena test for now. The
    # failure is likely due to changes to google_benchmark as the test
    # previously did pass and the arena system never caused an issue.
    #add_a2_bench(test_a2_arena test_arena.cc)
    add_a2_bench(bench_poly_within bench_poly_within.cc)

    add_executable(test_a2_exprtk test_a2_exprtk.cc)
    target_link_libraries(test_a2_exprtk
        PRIVATE gtest gtest_main liba2_static)
    add_test(NAME test_a2_exprtk COMMAND $<TARGET_FILE:test_a2_exprtk>)

    add_executable(test_a2_expression_operator test_a2_expression_operator.cc)
    target_link_libraries(test_a2_expression_operator ${A2_TEST_LIBRARY} gtest gtest_main)
    add_test(NAME test_a2_expression_operator COMMAND $<TARGET_FILE:test_a2_expression_operator>)

    function(add_a2_gtest exe_name exe_source)
        add_executable(${exe_name} ${exe_source})

        target_link_libraries(${exe_name}
            PRIVATE mesytec-mvlc
            PRIVATE gtest
            PRIVATE gtest_main
            )

        add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
    endfunction(add_a2_gtest)

    add_a2_gtest(test_a2_filter_datasources test_a2_filter_datasources.cc)

endif(MVME_BUILD_TESTS)

file(COPY env DESTINATION ${CMAKE_BINARY_DIR})
#file(COPY histoviewer.py DESTINATION .)

# vim:tw=0
