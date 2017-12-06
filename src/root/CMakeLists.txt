# https://root.cern.ch/how/integrate-root-my-project-cmake

list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

find_package(
    ROOT
    COMPONENTS Core RIO Tree Hist)

if (ROOT_FOUND)
    include(${ROOT_USE_FILE})

    find_program(ROOT_CONFIG_EXECUTABLE root-config
        HINTS $ENV{ROOTSYS}/bin)

    execute_process(
        COMMAND ${ROOT_CONFIG_EXECUTABLE} --prefix
        OUTPUT_VARIABLE ROOTSYS
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    message("-- Found ROOT in ROOTSYS=${ROOTSYS}")

    include_directories(${CMAKE_CURRENT_SOURCE_DIR})

    set(mvme_root_LIBS
        ${ROOT_LIBRARIES}
        ${ROOT_Core_LIBRARY}
        ${ROOT_RIO_LIBRARY}
        ${ROOT_Tree_LIBRARY}
        ${ROOT_Hist_LIBRARY}
        )

    ROOT_GENERATE_DICTIONARY(G__mvme_root mvme_root.h LINKDEF mvme_linkdef.h OPTIONS NOINSTALL)

    add_library(mvme_root SHARED mvme_root.cc G__mvme_root.cxx)
    target_link_libraries(mvme_root ${mvme_root_LIBS})

    add_executable(root_test root_test.cc)
    target_link_libraries(root_test mvme_root)

    add_executable(mvme-root-writer writer_process_main.cc)
    qt5_use_modules(mvme-root-writer Core Widgets)
    target_link_libraries(mvme-root-writer mvme_root)
endif(ROOT_FOUND)
