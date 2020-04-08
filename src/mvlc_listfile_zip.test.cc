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

TEST(mvlc_listfile_zip, MinizipCreate)
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

TEST(mvlc_listfile_zip, CreateWriteRead3)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreate3.zip";

    {
        ZipCreator creator;
        creator.createArchive(archiveName);

        {
            // Write outData0 two times
            auto &writeHandle = *creator.createZIPEntry("outfile0.data", 1);
            writeHandle.write(outData0.data(), outData0.size());
            writeHandle.write(outData0.data(), outData0.size());
            creator.closeCurrentEntry();
        }

#if 1
        {
            auto &writeHandle = *creator.createLZ4Entry("outfile1.data", 1);
            writeHandle.write(outData1.data(), outData1.size());
            creator.closeCurrentEntry();
        }
#endif
    }

#if 0
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
#endif
}

#if 1
TEST(mvlc_listfile_zip, ListfileCreateLarge3)
{

#if 0
    std::vector<u8> outData0(Megabytes(1));
    {
        std::random_device rd;
        std::default_random_engine engine(rd());
        std::uniform_int_distribution<unsigned> dist(0u, 255u);
        for (auto &c: outData0)
            c = static_cast<u8>(dist(engine));
    }
#elif 0
    std::vector<u8> outData0(Megabytes(1));
    {
        for (size_t i=0; i<outData0.size(); i++)
            outData0[i] = i;
    }
#elif 1
    std::vector<u32> outData0(Gigabytes(1) / sizeof(u32));

    auto out = std::begin(outData0);
    auto end = std::end(outData0);

    struct end_of_buffer {};

    auto put = [&out, &end] (u32 value)
    {
        if (out >= end)
            throw end_of_buffer();
        *out++ = value;
    };

    cout << "generating data" << endl;

    try
    {
        u32 eventNumber = 0;
        const unsigned ChannelCount = 32;
        const unsigned ChannelDataBits = 13;
        const unsigned ChannelNumberShift = 24;
        const u32 Header = 0xA000012; // some thought up header value

        std::random_device rd;
        std::default_random_engine engine(rd());
        std::uniform_int_distribution<u16> dist(0u, (1u << ChannelDataBits) - 1 );

        while (true)
        {
            put(Header);

            for (unsigned chan=0; chan<ChannelCount; chan++)
                put((chan << ChannelNumberShift) | dist(engine));

            put(eventNumber++);
        }
    }
    catch (const end_of_buffer &)
    { }

    cout << "done generating data" << endl;


#endif

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreateLarge3.zip";

    const size_t totalBytesToWrite = Gigabytes(5);
    size_t totalBytes = 0u;
    size_t writeCount = 0;
    ZipCreator::EntryInfo entryInfo = {};

    auto tStart = std::chrono::steady_clock::now();

    {
        ZipCreator creator;
        creator.createArchive(archiveName);
        auto &writeHandle = *creator.createLZ4Entry("outfile0.data", 0);
        //auto &writeHandle = *creator.createZIPEntry("outfile0.data", 1);

        do
        {
            auto raw = reinterpret_cast<const u8 *>(outData0.data());
            auto bytes = outData0.size() / sizeof(outData0.at(0));
            size_t bytesWritten = writeHandle.write(raw, bytes);
            totalBytes += bytesWritten;
            ++writeCount;
        } while (totalBytes < totalBytesToWrite);

        creator.closeCurrentEntry();

        entryInfo = creator.entryInfo();
    }

    auto tEnd = std::chrono::steady_clock::now();
    auto elapsed = tEnd - tStart;
    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

    cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
    cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
    cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
    cout << "EntryInfo: bytesWritten=" << entryInfo.bytesWritten
        << ", lz4CompressedBytesWritten=" << entryInfo.lz4CompressedBytesWritten
        << ", ratio=" << (entryInfo.bytesWritten / static_cast<double>(entryInfo.lz4CompressedBytesWritten))
        << endl;
}
#endif
