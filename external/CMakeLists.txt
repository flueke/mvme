# PCG - random number generator
add_library(pcg INTERFACE)
target_include_directories(pcg INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/pcg-cpp-0.98/include)

# zstr - C++ ZLib wrapper
add_library(zstr INTERFACE)
target_include_directories(zstr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/zstr/src)

# jcon-cpp
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/jcon-cpp/src/jcon/")

# nlohmann json.hpp
add_library(nlohmann-json INTERFACE)
target_include_directories(nlohmann-json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# exprtk
add_library(exprtk INTERFACE)
target_include_directories(exprtk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/exprtk)

# cpp11-on-multicore
add_library(cpp11-on-multicore INTERFACE)
target_include_directories(cpp11-on-multicore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
