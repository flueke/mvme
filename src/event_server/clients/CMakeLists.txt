# A small standalone test client for the mvme analysis event_server component.
add_executable(mvme_event_server_example_client event_server_example_client.cc)

target_include_directories(mvme_event_server_example_client
    PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty
    )

if (WIN32)
    target_link_libraries(mvme_event_server_example_client ws2_32)
endif (WIN32)

install(TARGETS mvme_event_server_example_client DESTINATION .)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_client/"
    DESTINATION share/mvme_root_client)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_client/mvme_root_event_objects.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_client/mvme_root_event_objects_LinkDef.h"
    DESTINATION include/mvme
    )

# ROOT
# https://root.cern.ch/how/integrate-root-my-project-cmake

###option(ENABLE_ROOT "Enable ROOT support" OFF)
###
###if (ENABLE_ROOT)
###    list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})
###
###    find_package(ROOT COMPONENTS Core RIO Tree Hist)
###
###    if (ROOT_FOUND)
###        #include(${ROOT_USE_FILE})
###        # FIXME: /home/florian/local/root/cmake/ROOTUseFile.cmake does include a file
###        # called "RootNewMacros.cmake" in which ROOT_GENERATE_DICTIONARY is defined. Do
###        # include this single file here.
###        # Basically the only problem I have with including ROOT_USE_FILE directly is that
###        # it changes the CFLAGS and in my case adds -DNDEBUG to the compiler command line
###        # which in turn disables assertions. This is not what I want when doing debug
###        # builds...
###        include_directories(${ROOT_INCLUDE_DIRS})
###        link_directories(${ROOT_LIBRARY_DIR})
###        # XXX: This contain -DNDEBUG
###        #add_definitions(${ROOT_DEFINITIONS})
###
###        find_program(ROOT_CONFIG_EXECUTABLE root-config
###            HINTS $ENV{ROOTSYS}/bin)
###
###        message("-- Found ROOT in ${ROOT_USE_FILE}")
###        message("-- Deliberately ignored ROOT_CXX_FLAGS: ${ROOT_CXX_FLAGS}")
###    endif(ROOT_FOUND)
###endif(ENABLE_ROOT)
###
###if (ENABLE_ROOT AND ROOT_FOUND)
###if (False) # TODO: port the mvme_root_treewriter_client2.cc code
###    set(root_LIBS
###        ${ROOT_LIBRARIES}
###        ${ROOT_Core_LIBRARY}
###        ${ROOT_RIO_LIBRARY}
###        ${ROOT_Tree_LIBRARY}
###        ${ROOT_Hist_LIBRARY}
###        )
###
###    # libmvme_root_event
###    add_custom_command(
###        OUTPUT mvme_root_event_rdict.cxx
###            G__mvme_root_event_objects_rdict.cxx
###            G__mvme_root_event_objects_rdict_rdict.pcm
###        COMMAND rootcling -f mvme_root_event_rdict.cxx
###            ${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_event_objects.h
###            ${CMAKE_CURRENT_SOURCE_DIR}/mvme_root_event_objects_LinkDef.h
###        COMMAND ${CMAKE_COMMAND} -E copy
###                ${CMAKE_CURRENT_BINARY_DIR}/mvme_root_event_rdict_rdict.pcm
###                ${CMAKE_BINARY_DIR}/
###        DEPENDS
###            mvme_root_event_objects.h
###            mvme_root_event_objects_LinkDef.h
###    )
###
###    set_property(SOURCE  mvme_root_event_rdict.cxx PROPERTY SKIP_AUTOMOC ON)
###    #set_property(SOURCE G__mvme_root_event_objects_rdict.cxx PROPERTY SKIP_AUTOMOC ON)
###
###    add_library(mvme_root_event SHARED
###        mvme_root_event_objects.cxx
###        mvme_root_event_rdict.cxx
###        )
###    target_link_libraries(mvme_root_event ${root_LIBS})
###    install(FILES G__mvme_root_event_objects_rdict.pcm DESTINATION .)
###
###    # Build the ROOT TTree writer client. This is a stand-alone, command-line
###    # client speaking the mvme analysis data server protocol.
###    #add_executable(mvme_root_treewriter_client event_server/mvme_root_treewriter_client.cc)
###    #target_link_libraries(mvme_root_treewriter_client ${root_LIBS})
###
###    #include(${ROOT_USE_FILE}) # FIXME: this might change CXXFLAGS globally. verify!
###    #ROOT_GENERATE_DICTIONARY(
###    #    mvme_root_export_objects
###    #    data_export/mvme_root_export_objects.h
###    #    LINKDEF data_export/mvme_root_export_objects_LinkDef.h)
###
###    add_executable(mvme_root_treewriter_client2 mvme_root_treewriter_client2.cc)
###    #${CMAKE_CURRENT_SOURCE_DIR}/G__mvme_root.cxx)
###    target_link_libraries(mvme_root_treewriter_client2 mvme_root_event)
###
###
###    install(FILES event_server/mvme_root_event_objects.h DESTINATION .)
###    ## copy to buildroot at cmake time
###    #configure_file(event_server/mvme_root_event_objects.h
###    #    ${CMAKE_BINARY_DIR}/mvme_root_event_objects.h
###    #    COPYONLY)
###endif()
###if (False)
###    # https://cliutils.gitlab.io/modern-cmake/chapters/packages/ROOT.html
###    # FIXME: The next line messes with the global include directories somehow.
###    include("${ROOT_DIR}/modules/RootNewMacros.cmake")
###    #include_directories(${CMAKE_CURRENT_SOURCE_DIR})
###    #root_generate_dictionary(G__mvme_root_event event_server/mvme_root_event_objects.h
###    #    LINKDEF event_server/mvme_root_event_objects_LinkDef.h
###    #    MODULE mvme_root_event
###    #    NOINSTALL)
###
###    #set_property(SOURCE G__mvme_root_event.cxx PROPERTY SKIP_AUTOMOC ON)
###
###    #add_library(mvme_root_event SHARED
###    #    event_server/mvme_root_event_objects.cxx
###    #    G__mvme_root_event.cxx
###    #    )
###    #target_link_libraries(mvme_root_event ${ROOT_LIBRARIES})
###
###    #add_executable(mvme_root_treewriter_client2 event_server/mvme_root_treewriter_client2.cc)
###    #target_link_libraries(mvme_root_treewriter_client2 mvme_root_event)
###endif()
###endif(ENABLE_ROOT AND ROOT_FOUND)

