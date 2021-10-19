find_package(Boost REQUIRED)
add_library(libmvme_eventbuilder SHARED event_builder.cc)
target_compile_options(libmvme_eventbuilder
    PRIVATE $<${not-msvc}:-Wall -Wextra>
    PRIVATE $<${not-msvc}:-Wno-deprecated-declarations> # disable Qt deprecation warnings
    )
set_target_properties(libmvme_eventbuilder PROPERTIES OUTPUT_NAME mvme_eventbuilder)
generate_export_header(libmvme_eventbuilder)
target_link_libraries(libmvme_eventbuilder
    PUBLIC libmvme_core
    PUBLIC mesytec-mvlc
    )
install(TARGETS libmvme_eventbuilder
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)
