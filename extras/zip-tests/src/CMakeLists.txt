# libzip
add_executable(libzip-read-test libzip-read-main.cc)
target_link_libraries(libzip-read-test PRIVATE zip)

add_executable(libzip-write-test libzip-write-main.cc)
target_link_libraries(libzip-write-test PRIVATE zip)

# minizip
add_executable(minizip-write-test minizip-write-main.cc)
target_link_libraries(minizip-write-test PRIVATE minizip)
