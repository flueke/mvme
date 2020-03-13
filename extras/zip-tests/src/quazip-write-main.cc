#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <quazip.h>
#include <quazipfile.h>
#include <QString>

using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        cout << "Usage: " << argv[0] << " <out_zipfile> <in_file>" << endl;
        return 1;
    }

    QString outFilename = argv[1];
    QString inFilename = argv[2];

    std::ifstream input(inFilename.toStdString(), std::ios::binary);

    if (!input.is_open())
        return 2;

    QuaZip archive;
    archive.setZipName(outFilename);
    //archive.setZip64Enabled(true);

    if (!archive.open(QuaZip::mdCreate))
        return 3;

    QuaZipNewInfo outFileInfo("testfile");
    QuaZipFile outFile(&archive);
    if (!outFile.open(QIODevice::WriteOnly, outFileInfo,
                      nullptr, 0, // password, crc (must be 0)
                      Z_DEFLATED, // compression method
                      1)) // compression level
    {
        return 4;
    }

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

        auto written = outFile.write(buffer.data(), len);

        ++writeCount;
        totalBytes += written;

        if (written != len)
            return 5;
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;

    return 0;
}

