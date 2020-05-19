# TODO: it might be better to create and link against a separate event_server library
# instead of directly adding stuff to libmvme
target_sources(libmvme PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/event_server_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/event_server.cc)
