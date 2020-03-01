add_library(mesytec-mvlc SHARED
    util/ticketmutex.cc
    )

target_include_directories(mesytec-mvlc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mesytec-mvlc
    PRIVATE ftd3xx
    )

target_compile_options(mesytec-mvlc PRIVATE -Wall -Wextra)
