/*

Purpose of the mvme_listfile_reader program:

- Read mvme listfiles and pass readout data to handler code.
- Accept any type of mvme listfile types (MVMELST, MVLC_*) both as a flat file
  and within a ZIP archive.
- Load user specified code and invoke it with data extracted from the listfile.

mvme_listfile_reader
    input_listfile_filename0, input_listfile_filename1, ...

    --dump-data / --no-dump-data
    --plugin my_c_plugin.so,--foo=bar,-osomething
    --plugin my_qt_plugin.so

Notes:
- There is no need to specify the library extension when using
  QPluginLoader or QLibrary.

- C-style libraries need to be wrapped in an extern "C" block if compiled using
  a c++ compiler.

- make the dumper/printer code a plugin too and prepend it to the list of
  default plugins. Remove it if --no-print-data is specified.

*/

int main(int argc, char *argv[])
{
    std::vector<std::string> inputFilenames;
    std::vector<std::string> pluginSpecs;

    return 0;
}
