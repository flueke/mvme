#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <mz.h>
#include <mz_zip.h>
#include <mz_strm.h>
#include <mz_zip_rw.h>
#include <mz_strm_os.h>

using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <out_zipfile> <in_file>" << endl;
        return 1;
    }

    std::string outFilename = argv[1];
    std::string inFilename = argv[2];

    std::ifstream input(inFilename, std::ios::binary);

    if (!input.is_open())
        return 2;

    void *writer = nullptr;
    void *file_stream = nullptr;

    mz_zip_writer_create(&writer);
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer, 1);

    mz_stream_os_create(&file_stream);

    if (auto err = mz_stream_os_open(file_stream, outFilename.c_str(), MZ_OPEN_MODE_CREATE | MZ_OPEN_MODE_WRITE))
        return 3;

    if (auto err = mz_zip_writer_open(writer, file_stream))
        return 4;

    mz_zip_file file_info = {};
    file_info.filename = "testfile";
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    //file_info.zip64 = MZ_ZIP64_FORCE;

    if (auto err = mz_zip_writer_entry_open(writer, &file_info))
        return 5;

    size_t writeCount = 0;
    size_t totalBytes = 0;
    std::array<char, 1024 * 1024> buffer;

    auto tStart = std::chrono::steady_clock::now();

    while (true)
    {
        input.read(buffer.data(), buffer.size());
        auto len = input.gcount();

        if (len == 0)
            break;

        int written = mz_zip_writer_entry_write(writer, buffer.data(), len);

        ++writeCount;
        totalBytes += len;

        //cout << "written=" << written << ", len=" << len << endl;

        if (written != len)
            return 6;
    }

    mz_zip_writer_close(writer);
    mz_stream_os_delete(&file_stream);
    mz_zip_writer_delete(&writer);

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;

    return 0;
}
