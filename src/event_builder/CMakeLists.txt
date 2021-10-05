find_package(Boost REQUIRED)
add_library(libmvme_eventbuilder SHARED event_builder.cc)
target_link_libraries(libmvme_eventbuilder
    PRIVATE libmvme_core
    PRIVATE mesytec-mvlc
    )
