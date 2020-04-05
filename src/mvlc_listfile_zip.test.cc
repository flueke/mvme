#include <iostream>

#include "gtest/gtest.h"

#include "mvlc_listfile_zip.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

TEST(mvlc_listfile_zip, CreateWriteRead)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    {
        ListfileZIPHandle writer;

        writer.open(
            "mvlc_listfile_zip.test.CreateWriteRead.zip",
            "outfile0.data",
            std::ios_base::out | std::ios_base::trunc);

        writer.write(outData0.data(), outData0.size());

        writer.close();

        writer.open(
            "mvlc_listfile_zip.test.CreateWriteRead.zip",
            "outfile1.data",
            std::ios_base::out);

        writer.write(outData1.data(), outData1.size());
    }

    {
        std::vector<u8> buffer(4);

        ListfileZIPHandle reader;

        reader.open(
            "mvlc_listfile_zip.test.CreateWriteRead.zip",
            "outfile0.data",
            std::ios_base::in);

        size_t bytesRead = reader.read(buffer.data(), buffer.size());

        ASSERT_EQ(bytesRead, 4);
        ASSERT_EQ(buffer, outData0);

        reader.open(
            "mvlc_listfile_zip.test.CreateWriteRead.zip",
            "outfile1.data",
            std::ios_base::in);

        bytesRead = reader.read(buffer.data(), buffer.size());

        ASSERT_EQ(bytesRead, 4);
        ASSERT_EQ(buffer, outData1);
    }
}
