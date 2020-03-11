add_executable(zip-read-test zip-read-main.cc)
target_link_libraries(zip-read-test PRIVATE zip)

add_executable(zip-write-test zip-write-main.cc)
target_link_libraries(zip-write-test PRIVATE zip)
