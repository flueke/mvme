# copy only
add_executable(copy-write-test copy-write-main.cc)

# libzip
add_executable(libzip-read-test libzip-read-main.cc)
target_link_libraries(libzip-read-test PRIVATE zip)

add_executable(libzip-write-test libzip-write-main.cc)
target_link_libraries(libzip-write-test PRIVATE zip)

# minizip
add_executable(minizip-write-test minizip-write-main.cc)
target_link_libraries(minizip-write-test PRIVATE minizip)

# quazip
find_package(Qt5Core REQUIRED)

find_path(QUAZIP_INCLUDE_DIR
    NAMES quazip.h
    PATH_SUFFIXES quazip quazip-qt5 quazip5)

find_library(QUAZIP_QT5_RELEASE NAMES quazip quazip-qt5 quazip5)
find_library(QUAZIP_QT5_DEBUG NAMES quazipd quazip-qt5d quazip5d)

if (WIN32 OR QUAZIP_QT5_DEBUG)
    set(QUAZIP_QT5
        optimized ${QUAZIP_QT5_RELEASE}
        debug ${QUAZIP_QT5_DEBUG}
        )
else()
    set(QUAZIP_QT5 ${QUAZIP_QT5_RELEASE})
endif()

add_executable(quazip-write-test quazip-write-main.cc)
target_link_libraries(quazip-write-test
    PRIVATE ${QUAZIP_QT5} Qt5::Core
    )
target_include_directories(quazip-write-test
    PRIVATE ${QUAZIP_INCLUDE_DIR}
    )
