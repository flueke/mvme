#include <chrono>
#include <random>
#include <iostream>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "gtest/gtest.h"

#include "mvlc_listfile_zip.h"
#include "util/storage_sizes.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

#if 0
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
#endif

TEST(mvlc_listfile_zip, MinizipCreateWriteRead)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.MinizipCreateWriteRead.zip";
    void *osStream = nullptr;
    void *zipWriter = nullptr;
    s32 mzMode = MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE;

    mz_stream_os_create(&osStream);
    mz_zip_writer_create(&zipWriter);
    mz_zip_writer_set_compress_method(zipWriter, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(zipWriter, 1);
    mz_zip_writer_set_follow_links(zipWriter, true);

    if (auto err = mz_stream_os_open(osStream, archiveName.c_str(), mzMode))
        throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

    if (auto err = mz_zip_writer_open(zipWriter, osStream))
        throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));

    // outfile0
    {
        std::string m_entryName = "outfile0.data";

        mz_zip_file file_info = {};
        file_info.filename = m_entryName.c_str();
        file_info.modified_date = time(nullptr);
        file_info.version_madeby = MZ_VERSION_MADEBY;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_FORCE;
        file_info.external_fa = (S_IFREG) | (0644u << 16);

        if (auto err = mz_zip_writer_entry_open(zipWriter, &file_info))
            throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

        {
            s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData0.data(), outData0.size());

            if (bytesWritten < 0)
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

            if (static_cast<size_t>(bytesWritten) != outData0.size())
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        }

        {
            s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData0.data(), outData0.size());

            if (bytesWritten < 0)
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

            if (static_cast<size_t>(bytesWritten) != outData0.size())
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));
        }

        if (auto err = mz_zip_writer_entry_close(zipWriter))
            throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));
    }

#if 1
    // outfile1
    {
        std::string m_entryName = "outfile1.data";

        mz_zip_file file_info = {};
        file_info.filename = m_entryName.c_str();
        file_info.modified_date = time(nullptr);
        file_info.version_madeby = MZ_VERSION_MADEBY;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_FORCE;
        file_info.external_fa = (S_IFREG) | (0644u << 16);

        if (auto err = mz_zip_writer_entry_open(zipWriter, &file_info))
            throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

        s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData1.data(), outData1.size());

        if (bytesWritten < 0)
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        if (static_cast<size_t>(bytesWritten) != outData1.size())
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        if (auto err = mz_zip_writer_entry_close(zipWriter))
            throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));
    }
#endif

    if (auto err = mz_zip_writer_close(zipWriter))
        throw std::runtime_error("mz_zip_writer_close: " + std::to_string(err));

    if (auto err = mz_stream_os_close(osStream))
        throw std::runtime_error("mz_stream_os_close: " + std::to_string(err));

    mz_zip_writer_delete(&zipWriter);
    mz_stream_os_delete(&osStream);

}

#if 0
TEST(mvlc_listfile_zip, ListfileCreate)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreate.zip";

    ZipCreator creator;
    creator.createArchive(archiveName);

    {
        auto &writeHandle = *creator.createEntry("outfile0.data");
        writeHandle.write(outData0.data(), outData0.size());
        writeHandle.write(outData0.data(), outData0.size());
    }

    {
        auto &writeHandle = *creator.createEntry("outfile1.data");
        writeHandle.write(outData1.data(), outData1.size());
    }
}
#endif

#if 0
TEST(mvlc_listfile_zip, ListfileCreateLarge)
{
    std::vector<u8> outData0(Megabytes(1));

#if 1
    {
        std::random_device rd;
        std::default_random_engine engine(rd());
        std::uniform_int_distribution<unsigned> dist(0u, 255u);
        for (auto &c: outData0)
            c = static_cast<u8>(dist(engine));
    }
#else
    {
        for (size_t i=0; i<outData0.size(); i++)
            outData0[i] = i;
    }
#endif

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreateLarge.zip";

    const size_t totalBytesToWrite = Gigabytes(4);
    size_t totalBytes = 0u;
    size_t writeCount = 0;

    auto tStart = std::chrono::steady_clock::now();

    {
        ZipCreator creator;
        creator.createArchive(archiveName);
        auto &writeHandle = *creator.createEntry("outfile0.data");

        do
        {
            size_t bytesWritten = writeHandle.write(outData0.data(), outData0.size());
            totalBytes += bytesWritten;
            ++writeCount;
        } while (totalBytes < totalBytesToWrite);
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
}
#endif

TEST(mvlc_listfile_zip, CreateWriteRead2)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreate2.zip";

    {
        ZipCreator2 creator;
        creator.createArchive(archiveName);

        {
            // Write outData0 two times
            auto &writeHandle = *creator.createEntry("outfile0.data");
            writeHandle.write(outData0.data(), outData0.size());
            writeHandle.write(outData0.data(), outData0.size());
        }

        {
            auto &writeHandle = *creator.createEntry("outfile1.data");
            writeHandle.write(outData1.data(), outData1.size());
        }
    }

    {
        ZipReader reader;
        reader.openArchive(archiveName);

        auto entryList = reader.entryList();
        std::vector<std::string> expectedEntries = { "outfile0.data", "outfile1.data" };

        ASSERT_EQ(entryList, expectedEntries);

        auto readHandle = reader.openEntry("outfile0.data");

        {
            // Read outData0 two times
            std::vector<u8> inData0(outData0.size());
            size_t bytesRead = readHandle->read(inData0.data(), inData0.size());

            ASSERT_EQ(bytesRead, outData0.size());
            ASSERT_EQ(inData0, outData0);

            bytesRead = readHandle->read(inData0.data(), inData0.size());
            ASSERT_EQ(bytesRead, outData0.size());
            ASSERT_EQ(inData0, outData0);

            // Third read should yield 0 bytes as we're at the end of the entry.
            bytesRead = readHandle->read(inData0.data(), inData0.size());
            ASSERT_EQ(bytesRead, 0);
        }

        {
            // Restart reading from the beginning of the entry.
            readHandle->seek(0);
            std::vector<u8> inData0(outData0.size() * 2);
            size_t bytesRead = readHandle->read(inData0.data(), inData0.size());

            const std::vector<u8> outData0_2 = {
                0x12, 0x34, 0x56, 0x78,
                0x12, 0x34, 0x56, 0x78
            };

            ASSERT_EQ(bytesRead, outData0.size() * 2);
            ASSERT_EQ(inData0, outData0_2);
        }
    }
}

#if 0
TEST(mvlc_listfile_zip, ListfileCreateLarge2)
{
    std::vector<u8> outData0(Megabytes(1));

#if 0
    {
        std::random_device rd;
        std::default_random_engine engine(rd());
        std::uniform_int_distribution<unsigned> dist(0u, 255u);
        for (auto &c: outData0)
            c = static_cast<u8>(dist(engine));
    }
#else
    {
        for (size_t i=0; i<outData0.size(); i++)
            outData0[i] = i;
    }
#endif

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreateLarge2.zip";

    const size_t totalBytesToWrite = Gigabytes(4);
    size_t totalBytes = 0u;
    size_t writeCount = 0;

    auto tStart = std::chrono::steady_clock::now();

    {
        ZipCreator2 creator;
        creator.createArchive(archiveName);
        auto &writeHandle = *creator.createEntry("outfile0.data");

        do
        {
            size_t bytesWritten = writeHandle.write(outData0.data(), outData0.size());
            totalBytes += bytesWritten;
            ++writeCount;
        } while (totalBytes < totalBytesToWrite);
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
}
#endif
