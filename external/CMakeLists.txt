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
    set(NNG_TESTS OFF)
    set(NNG_ENABLE_NNGCAT OFF)
    set(BUILD_SHARED_LIBS ON)
    add_subdirectory(nng)
    install(TARGETS nng)
endif()
