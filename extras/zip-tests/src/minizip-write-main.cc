#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <mz_zip.h>

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

    auto tStart = std::chrono::steady_clock::now();


    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    //auto megaBytesPerSecond = (sb.totalBytes / (1024 * 1024)) / seconds;

    //cout << "Wrote the listfile in " << sb.writeCount << " iterations, totalBytes=" << sb.totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    //cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;

    return 0;
}

