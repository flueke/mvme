pybind11_add_module(mesytec_mvlc_python mesytec_mvlc_python.cc)
target_link_libraries(mesytec_mvlc_python PRIVATE mesytec-mvlc)

install(TARGETS mesytec_mvlc_python
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    )
