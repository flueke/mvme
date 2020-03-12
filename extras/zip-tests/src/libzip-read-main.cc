#include <array>
#include <iostream>
#include <string>
#include <chrono>

#include <zip.h>

using std::cout;
using std::endl;

bool ends_with(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cout << "Usage: " << argv[0] << " <zipfile>" << endl;
        return 1;
    }

    int zipError = 0;

    zip_t *zipHandle = zip_open(argv[1], ZIP_RDONLY, &zipError);

    if (!zipHandle)
        return 1;

    zip_int64_t entryCount = zip_get_num_entries(zipHandle, 0);
    zip_stat_t listfileStat = {};

    for (zip_int64_t zipIndex = 0; zipIndex < entryCount; ++zipIndex)
    {
        zip_stat_t stat = {};
        if (zip_stat_index(zipHandle, zipIndex, 0, &stat) != 0)
            return 1;

        cout << "zipIndex" << zipIndex << ": name=" << stat.name
            << ", index=" << stat.index << ", size=" << stat.size
            << ", comp_size=" << stat.comp_size << endl;

        if (ends_with(std::string(stat.name), ".mvmelst"))
            listfileStat = stat;
    }

    if (listfileStat.name)
    {
        cout << "reading listfile " << listfileStat.name << " from zip..." << endl;
        std::array<char, 1024 * 1024> buffer;
        zip_file_t *fileHandle = zip_fopen_index(zipHandle, listfileStat.index, 0);

        if (!fileHandle)
            return 1;

        size_t readCount = 0;
        size_t totalBytes = 0;
        auto tStart = std::chrono::steady_clock::now();
        while (true)
        {
            zip_int64_t bytesRead = zip_fread(fileHandle, buffer.data(), buffer.size());

            if (bytesRead <= 0)
                break;

            totalBytes += bytesRead;
            ++readCount;

            //cout << "Read " << bytesRead << " bytes from the listfile" << endl;


#if 0
            auto raw = reinterpret_cast<uint32_t *>(buffer.data());
            auto rawSize = buffer.size() / sizeof(uint32_t);

            for (size_t i=0; i<rawSize;i++)
            {
                cout << std::hex << raw[i] << std::dec << std::endl;
            }
#endif
        }
        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

        cout << "Read the listfile in " << readCount << " iterations, totalBytes=" << totalBytes << endl;
        cout << "Reading took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
    }

    zip_close(zipHandle);

    return 0;
}
