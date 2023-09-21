set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Test REQUIRED)
find_package(Qt5Widgets REQUIRED)

include_directories(../src)
include_directories("${CMAKE_BINARY_DIR}/src")

function(add_mvme_test exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    target_link_libraries(${exe_name}
        PRIVATE libmvme
        PRIVATE Qt5::Gui
        PRIVATE Qt5::Test
        PRIVATE Qt5::Widgets
        )
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_test)

# IMPORTANT: When creating a new test you do need to create a new header file
# for that test too. Using a combined header file for all test classes does
# not work as automoc/cmake/something else fails to generate any actual MOC
# code and we end up with undefined references to the vtable.

add_mvme_test(test_data_filter test_data_filter.cc)
target_link_libraries(test_data_filter PRIVATE liba2_static)
#add_mvme_test(test_analysis_aggregate_ops test_analysis_aggregate_ops.cc)
add_mvme_test(test_leaky_bucket test_leaky_bucket.cc)

# google/benchmark based tests
find_package(Threads)

function(add_mvme_bench exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    target_link_libraries(${exe_name}
        PRIVATE libmvme
        PRIVATE benchmark::benchmark
        PRIVATE Threads::Threads)
    if (WIN32)
        target_link_libraries(${exe_name}
            PRIVATE shlwapi.lib)
    endif(WIN32)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_bench)

#add_mvme_bench(test_util_tree test_util_tree.cc)
add_mvme_bench(bench_data_filter bench_data_filter.cc)
target_link_libraries(bench_data_filter PRIVATE liba2_static)
add_mvme_bench(test_misc test_misc.cc)

# gtest tests

# For tests depending on libmvme_core
function(add_mvme_core_gtest exe_name exe_source)
    add_executable(${exe_name} ${exe_source})
    target_link_libraries(${exe_name}
        PRIVATE libmvme_core
        PRIVATE gtest
        PRIVATE gtest_main)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_core_gtest)

# For tests depending on libmvme
function(add_mvme_gtest exe_name exe_source)
    add_executable(${exe_name} ${exe_source})
    target_link_libraries(${exe_name}
        PRIVATE libmvme
        PRIVATE gtest
        PRIVATE gtest_main)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_gtest)

add_mvme_core_gtest(test_util_databuffer "test_util_databuffer.cc")
add_mvme_core_gtest(test_util_ticketmutex "test_util_ticketmutex.cc")
add_mvme_core_gtest(test_vme_script_parsing "test_vme_script_parsing.cc")
add_mvme_core_gtest(test_vme_script_variables "test_vme_script_variables.cc")
add_mvme_core_gtest(test_vme_script_commands "test_vme_script_commands.cc")
add_mvme_gtest(test_logfile_helper "logfile_helper.test.cc")
add_mvme_gtest(test_multi_event_splitter "multi_event_splitter.test.cc")
add_mvme_gtest(test_analysis_v3_to_v4_migration "analysis_v3_to_v4_migration.test.cc")
