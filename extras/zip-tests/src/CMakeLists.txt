# copy only
add_executable(copy-write-test copy-write-main.cc)

# minizip
add_executable(minizip-write-test minizip-write-main.cc)
target_link_libraries(minizip-write-test PRIVATE minizip)

find_package(Threads)
add_executable(minizip-threaded-write-test minizip-threaded-write-main.cc)
target_link_libraries(minizip-threaded-write-test PRIVATE mesytec-mvlc minizip
    Threads::Threads)
