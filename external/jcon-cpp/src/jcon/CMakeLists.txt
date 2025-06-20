project(jcon)

set(QTDIR $ENV{QTDIR})
set(Q5_DIR $ENV{QTDIR})

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

file(GLOB ${PROJECT_NAME}_headers *.h)
file(GLOB ${PROJECT_NAME}_sources *.cpp)

add_definitions(-DJCON_DLL)

add_library(${PROJECT_NAME} SHARED ${${PROJECT_NAME}_headers} ${${PROJECT_NAME}_sources})

find_package(Qt5 COMPONENTS Core Network Test WebSockets Widgets REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PUBLIC Qt5::Core
    PUBLIC Qt5::Network
    PUBLIC Qt5::Test
    PUBLIC Qt5::WebSockets
    PUBLIC Qt5::Widgets)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static)

#install(TARGETS ${PROJECT_NAME} DESTINATION .)
