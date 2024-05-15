# PCG - random number generator
add_library(pcg INTERFACE)
target_include_directories(pcg INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/pcg-cpp-0.98/include)

# zstr - C++ ZLib wrapper
add_library(zstr INTERFACE)
target_include_directories(zstr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/zstr/src)

# jcon-cpp
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/jcon-cpp/src/jcon/)
target_compile_options(jcon
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )

# nlohmann json.hpp
add_library(nlohmann-json INTERFACE)
target_include_directories(nlohmann-json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann-json)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann-json/nlohmann" DESTINATION include/mvme)

# Mustache template library
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Mustache/mustache.hpp" DESTINATION include/mvme/Mustache)

# exprtk
add_library(exprtk INTERFACE)
target_include_directories(exprtk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/exprtk)

# cpp11-on-multicore
add_library(cpp11-on-multicore INTERFACE)
target_include_directories(cpp11-on-multicore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cpp11-on-multicore)

# libmesytec-mvlc
add_subdirectory(mesytec-mvlc)

# install pybind11 to include/
INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/pybind11 DESTINATION include)

# googletest
if (MVME_BUILD_TESTS)
    if (NOT TARGET gtest)
        add_subdirectory(googletest)
    endif()
endif()

# qgv - qt graphviz library
add_subdirectory(qgv)

# nng - networking library
find_package(nng QUIET)

if(NOT nng)
    message("-- Using nng from external/nng")
    set(BUILD_SHARED_LIBS ON)
    set(NNG_TESTS OFF)
    set(NNG_ENABLE_NNGCAT ON)
    set(NNG_ENABLE_COMPAT OFF)
    set(NNG_RESOLV_CONCURRENCY 2 CACHE STRING "Resolver (DNS) concurrency.")
    set(NNG_NUM_TASKQ_THREADS 0 CACHE STRING "Fixed number of task threads, 0 for automatic")
    set(NNG_MAX_TASKQ_THREADS 4 CACHE STRING "Upper bound on task threads, 0 for no limit")
    set(NNG_NUM_EXPIRE_THREADS 0 CACHE STRING "Fixed number of expire threads, 0 for automatic")
    set(NNG_MAX_EXPIRE_THREADS 2 CACHE STRING "Upper bound on expire threads, 0 for no limit")
    set(NNG_NUM_POLLER_THREADS 0 CACHE STRING "Fixed number of I/O poller threads, 0 for automatic")
    set(NNG_MAX_POLLER_THREADS 2 CACHE STRING "Upper bound on I/O poller threads, 0 for no limit")
    add_subdirectory(nng)

    # nng sets these to "${PROJECT_BINARY_DIR}" which means the dll/a/so files
    # ends up in build/external/nng. When debugging under windows the loader
    # will not look there for libs. Solution: force output to end up in the
    # build root directory. Only needed when BUILD_SHARED_LIBS=ON!
    #set_target_properties(nng PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    #set_target_properties(nng PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    #install(TARGETS nng) # Only needed when BUILD_SHARED_LIBS=ON and/or the
    #headers should be installed.

    #if (LINUX)
    #    set_target_properties(nng PROPERTIES POSITION_INDEPENDENT_CODE ON)
    #endif()


endif()
