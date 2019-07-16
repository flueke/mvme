add_mvme_executable(listfile_reader "listfile_reader_main.cc")

add_library(listfile_reader_print_plugin SHARED listfile_reader_print_plugin.cc)

# vim:tw=0
