if (BUILD_TESTS)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
find_package(Qt5Test REQUIRED)
include_directories(../src)
include_directories("${CMAKE_BINARY_DIR}/src")

function(add_mvme_test exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    add_dependencies(${exe_name} libmvme)
    target_link_libraries(${exe_name} libmvme)
    qt5_use_modules(${exe_name} Test)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_test)

# IMPORTANT: When creating a new test you do need to create a new header file
# for that test too. Using a combined header file for all test classes does
# not work as automoc/cmake/something else fails to generate any actual MOC
# code and we end up with undefined references to the vtable.

add_mvme_test(test_data_filter test_data_filter.cc)
#add_mvme_test(test_analysis_aggregate_ops test_analysis_aggregate_ops.cc)
add_mvme_test(test_leaky_bucket test_leaky_bucket.cc)

# google/benchmark based tests
find_library(GOOGLE_BENCHMARK benchmark)
find_path(GOOGLE_BENCHMARK_INCLUDE_DIR "benchmark/benchmark.h")
include_directories(${GOOGLE_BENCHMARK_INCLUDE_DIR})
find_package(Threads)

function(add_mvme_bench exe_name exe_sources)
    add_executable(${exe_name} ${exe_sources})
    add_dependencies(${exe_name} libmvme)
    if (WIN32)
        target_link_libraries(${exe_name} libmvme ${GOOGLE_BENCHMARK} Threads::Threads shlwapi.lib)
    else(WIN32)
        target_link_libraries(${exe_name} libmvme ${GOOGLE_BENCHMARK} Threads::Threads)
    endif(WIN32)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_bench)

add_mvme_bench(test_util_tree test_util_tree.cc)
add_mvme_bench(bench_data_filter bench_data_filter.cc)
add_mvme_bench(test_misc test_misc.cc)

# gtest tests

function(add_mvme_core_gtest exe_name exe_source)
    add_executable(${exe_name} ${exe_source})
    add_dependencies(${exe_name} libmvme_core)
    target_link_libraries(${exe_name} libmvme_core gtest gtest_main)
    add_test(NAME ${exe_name} COMMAND $<TARGET_FILE:${exe_name}>)
endfunction(add_mvme_core_gtest)

add_mvme_core_gtest(test_util_databuffer "test_util_databuffer.cc")
add_mvme_core_gtest(test_util_ticketmutex "test_util_ticketmutex.cc")
add_mvme_core_gtest(test_vme_script_parsing "test_vme_script_parsing.cc")

endif(BUILD_TESTS)
