#include <iostream>

#include "gtest/gtest.h"

#include "mvlc_listfile_zip.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc::listfile;

TEST(mvlc_listfile_zip, CreateWriteRead)
{
    {
        ListfileZIPHandle writer;

        writer.open(
            "mvlc_listfile_zip.test.CreateWriteRead.zip",
            "the_testfile.data",
            std::ios_base::out | std::ios_base::trunc);
    }
}
