#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>

#include <zip.h>

using std::cout;
using std::endl;

struct StreamAndBuffer
{
    std::ifstream input;
    std::vector<char> buffer;
};

zip_int64_t my_zip_source_callback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd)
{
    cout << __PRETTY_FUNCTION__
        << "userdata=" << userdata
        << ", data=" << data
        << ", len=" << len
        << ", cmd=" << cmd << endl;

    auto &sb = *reinterpret_cast<StreamAndBuffer *>(data);

    switch (cmd)
    {
        case ZIP_SOURCE_SUPPORTS:
            return 0;

        case ZIP_SOURCE_OPEN:
            return 0;

        case ZIP_SOURCE_READ:
            {
                size_t readLen = std::min(len, sb.buffer.size());
                sb.input.read(sb.buffer.data(), readLen);
            }
            break;
        case ZIP_SOURCE_CLOSE:
        case ZIP_SOURCE_STAT:
        case ZIP_SOURCE_ERROR:
    }


    return -1;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <out_zipfile> <in_file>" << endl;
        return 1;
    }

    std::string outFilename = argv[1];
    std::string inFilename = argv[2];
    int zipError = 0;

    zip_t *zipHandle = zip_open(outFilename.c_str(), ZIP_CREATE, &zipError);

    if (!zipHandle)
        return 1;

    StreamAndBuffer sb
    {
        std::ifstream(inFilename, std::ios::binary),
        std::vector<char>(1024 * 1024)
    };

    if (!sb.input.is_open())
        return 1;

    zip_source_t *source = zip_source_function(
        zipHandle, my_zip_source_callback, &sb);

    if (!source)
        return 1;

    zip_int64_t newIndex = zip_file_add(zipHandle, "testfile", source, ZIP_FL_ENC_GUESS);

    if (newIndex < 0)
        return 1;

    zip_close(zipHandle);

    return 0;
}
