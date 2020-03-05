add_library(ftd3xx SHARED IMPORTED GLOBAL)

if (WIN32)
    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2/x64/FTD3XX.dll
        IMPORTED_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2/x64/FTD3XX.lib)

    target_include_directories(ftd3xx
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2)

    install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/FTD3XXLibrary_v1.3.0.2/x64/FTD3XX.dll
        DESTINATION lib)
elseif(UNIX AND NOT APPLE)
    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx.so)

    target_include_directories(ftd3xx
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/)

    install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/ftdi-d3xx-linux-x86_64/libftd3xx.so
        DESTINATION lib)
elseif(APPLE)
    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx/libftd3xx.dylib)

    target_include_directories(ftd3xx
        INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx)

    install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/d3xx-osx-0.5.21/osx/libftd3xx.dylib
        DESTINATION lib)
endif()
