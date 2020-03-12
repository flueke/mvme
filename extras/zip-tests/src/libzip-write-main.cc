#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <zip.h>

using std::cout;
using std::endl;

struct StreamAndBuffer
{
    std::ifstream input;
    std::string filename;
    size_t writeCount = 0;
    size_t totalBytes = 0;
};

zip_int64_t my_zip_source_callback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd)
{
    auto &sb = *reinterpret_cast<StreamAndBuffer *>(userdata);

#if 0
    cout << __PRETTY_FUNCTION__
        << " userdata=" << userdata
        << ", data=" << data
        << ", len=" << len
        << ", cmd=" << cmd
        << endl;


    cout << __PRETTY_FUNCTION__
        << " filename=" << sb.filename
        << endl;
#endif


    switch (cmd)
    {
        case ZIP_SOURCE_SUPPORTS:
            return zip_source_make_command_bitmap(
                ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT);

        case ZIP_SOURCE_OPEN:
            return 0;

        case ZIP_SOURCE_READ:
            {
                sb.input.read(reinterpret_cast<char *>(data), len);
                auto written = static_cast<zip_int64_t>(sb.input.gcount());
                ++sb.writeCount;
                sb.totalBytes += written;
                return written;
            }
            break;

        case ZIP_SOURCE_CLOSE:
            sb.input.close();
            return 0;

        case ZIP_SOURCE_STAT:
#if 1
            {
                auto zipStat = reinterpret_cast<zip_stat_t *>(data);
                zip_stat_init(zipStat);
                zipStat->valid = ZIP_STAT_NAME;
                //zipStat->name = reinterpret_cast<const char *>(std::malloc(sb.filename.size() + 1));
                //std::strncpy(zipStat->name, sb.filename.c_str(), sb.filename.size());
                cout << __PRETTY_FUNCTION__ << "passed in comp_method: " << zipStat->comp_method << endl;
                //zipStat->comp_method = ZIP_CM_SHRINK;
                //zipStat->comp_method = ZIP_CM_DEFLATE;
                //zipStat->comp_method = ZIP_CM_STORE;

                return sizeof(struct zip_stat);
            }
#else
            return 0;
#endif

        case ZIP_SOURCE_ERROR:
            return 0;

        default:
            break;
    }

    return 0;
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
        inFilename,
        0,
        0
    };

    if (!sb.input.is_open())
        return 1;

    auto tStart = std::chrono::steady_clock::now();

    zip_source_t *source = zip_source_function(
        zipHandle, my_zip_source_callback, &sb);

    if (!source)
        return 1;

    zip_int64_t newIndex = zip_file_add(zipHandle, "testfile", source, ZIP_FL_ENC_GUESS);

    if (newIndex < 0)
        return 1;

    zip_set_file_compression(
        zipHandle,
        newIndex,
        ZIP_CM_DEFLATE,
        0);

    cout << "pre zip close" << endl;
    zip_close(zipHandle);
    cout << "post zip close" << endl;

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (sb.totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << sb.writeCount << " iterations, totalBytes=" << sb.totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;

    return 0;
}
