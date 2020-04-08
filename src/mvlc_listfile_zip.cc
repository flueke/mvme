#include "mvlc_listfile_zip.h"

#include <cassert>
#include <lz4frame.h>
#include <mz.h>
#include <mz_compat.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <sys/stat.h>

#include "util/storage_sizes.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

ZipEntryWriteHandle::ZipEntryWriteHandle(ZipCreator *creator)
    : m_zipCreator(creator)
{ }

ZipEntryWriteHandle::~ZipEntryWriteHandle()
{ }

size_t ZipEntryWriteHandle::write(const u8 *data, size_t size)
{
    return m_zipCreator->writeToCurrentEntry(data, size);
}

struct ZipCreator::Private
{
    struct LZ4Context
    {
        struct CompressResult
        {
            int error;
            unsigned long long size_in;
            unsigned long long size_out;
        };

        static constexpr size_t ChunkSize = Megabytes(1);

        LZ4F_preferences_t lz4Prefs =
        {
            { LZ4F_max4MB, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_frame,
              0 /* unknown content size */, 0 /* no dictID */ , LZ4F_noBlockChecksum },
            0,   /* compression level; 0 == default */
            0,   /* autoflush */
            0,   /* favor decompression speed */
            { 0, 0, 0 },  /* reserved, must be set to 0 */
        };

        LZ4F_compressionContext_t ctx = {};
        std::vector<u8> buffer;

        void begin(int compressLevel)
        {
            if (auto err = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION))
                throw std::runtime_error("LZ4F_createCompressionContext: " + std::to_string(err));

            lz4Prefs.compressionLevel = compressLevel;
            size_t bufferSize = LZ4F_compressBound(ChunkSize, &lz4Prefs);
            buffer = std::vector<u8>(bufferSize);
        }

        void end()
        {
            LZ4F_freeCompressionContext(ctx);   /* supports free on NULL */
        }
    };

    explicit Private(ZipCreator *q_)
        : entryWriteHandle(q_)
    {
        mz_stream_os_create(&mz_osStream);
        mz_stream_buffered_create(&mz_bufStream);
        mz_stream_set_base(mz_bufStream, mz_osStream);

        mz_zip_writer_create(&mz_zipWriter);
        mz_zip_writer_set_follow_links(mz_zipWriter, true);
    }

    ~Private()
    {
        mz_zip_writer_delete(&mz_zipWriter);
        mz_stream_delete(&mz_bufStream);
        mz_stream_delete(&mz_osStream);

        mz_zipWriter = nullptr;
        mz_bufStream = nullptr;
        mz_osStream = nullptr;
    }

    size_t writeToCurrentZIPEntry(const u8 *data, size_t size)
    {
        s32 bytesWritten = mz_zip_writer_entry_write(mz_zipWriter, data, size);

        if (bytesWritten < 0)
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        assert(static_cast<size_t>(bytesWritten) == size);

        return static_cast<size_t>(bytesWritten);
    }

    void *mz_zipWriter = nullptr;
    void *mz_bufStream = nullptr;
    void *mz_osStream = nullptr;

    EntryInfo entryInfo;

    LZ4Context lz4Ctx;

    ZipEntryWriteHandle entryWriteHandle;
};

ZipCreator::ZipCreator()
    : d(std::make_unique<Private>(this))
{}

ZipCreator::~ZipCreator()
{
    try
    {
        closeCurrentEntry();
    }
    catch (...)
    {}

    try
    {
        closeArchive();
    }
    catch (...)
    {}

}

void ZipCreator::createArchive(const std::string &zipFilename)
{
    s32 mzMode = MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE;

    if (auto err = mz_stream_open(d->mz_bufStream, zipFilename.c_str(), mzMode))
        throw std::runtime_error("mz_stream_open: " + std::to_string(err));

    if (auto err = mz_zip_writer_open(d->mz_zipWriter, d->mz_bufStream))
        throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));
}

void ZipCreator::closeArchive()
{
    if (auto err = mz_zip_writer_close(d->mz_zipWriter))
        throw std::runtime_error("mz_zip_writer_close: " + std::to_string(err));

    if (auto err = mz_stream_close(d->mz_bufStream))
        throw std::runtime_error("mz_stream_close: " + std::to_string(err));
}

bool ZipCreator::isOpen() const
{
    return mz_stream_is_open(d->mz_bufStream) == MZ_OK;
}

ZipEntryWriteHandle *ZipCreator::createZIPEntry(const std::string &entryName, int compressLevel)
{
    if (hasOpenEntry())
        throw std::runtime_error("ZipCreator has open archive entry");

    mz_zip_file file_info = {};
    file_info.filename = entryName.c_str();
    file_info.modified_date = time(nullptr);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    file_info.zip64 = MZ_ZIP64_FORCE;
    file_info.external_fa = (S_IFREG) | (0644u << 16);

    mz_zip_writer_set_compress_method(d->mz_zipWriter, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(d->mz_zipWriter, compressLevel);

    if (auto err = mz_zip_writer_entry_open(d->mz_zipWriter, &file_info))
        throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

    d->entryInfo = {};
    d->entryInfo.type = EntryInfo::ZIP;
    d->entryInfo.name = entryName;
    d->entryInfo.isOpen = true;

    return &d->entryWriteHandle;
}

ZipEntryWriteHandle *ZipCreator::createLZ4Entry(const std::string &entryName_, int compressLevel)
{
    if (hasOpenEntry())
        throw std::runtime_error("ZipCreator has open archive entry");

    const std::string entryName = entryName_ + ".lz4";

    mz_zip_file file_info = {};
    file_info.filename = entryName.c_str();
    file_info.modified_date = time(nullptr);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_STORE;
    file_info.zip64 = MZ_ZIP64_FORCE;
    file_info.external_fa = (S_IFREG) | (0644u << 16);

    mz_zip_writer_set_compress_method(d->mz_zipWriter, MZ_COMPRESS_METHOD_STORE);
    mz_zip_writer_set_compress_level(d->mz_zipWriter, 0);

    if (auto err = mz_zip_writer_entry_open(d->mz_zipWriter, &file_info))
        throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

    d->entryInfo = {};
    d->entryInfo.type = EntryInfo::LZ4;
    d->entryInfo.name = entryName;
    d->entryInfo.isOpen = true;

    d->lz4Ctx.begin(compressLevel);

    // write the LZ4 frame header
    size_t lz4BufferBytes = LZ4F_compressBegin(
        d->lz4Ctx.ctx,
        d->lz4Ctx.buffer.data(),
        d->lz4Ctx.buffer.size(),
        &d->lz4Ctx.lz4Prefs);

    if (LZ4F_isError(lz4BufferBytes))
        throw std::runtime_error("LZ4F_compressBegin: " + std::to_string(lz4BufferBytes));

    // flush the LZ4 buffer contents to the ZIP
    d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), lz4BufferBytes);

    d->entryInfo.bytesWritten += lz4BufferBytes;
    d->entryInfo.lz4CompressedBytesWritten += lz4BufferBytes;

    return &d->entryWriteHandle;
}

bool ZipCreator::hasOpenEntry() const
{
    return d->entryInfo.isOpen;
}

const ZipCreator::EntryInfo &ZipCreator::entryInfo() const
{
    return d->entryInfo;
}

size_t ZipCreator::writeToCurrentEntry(const u8 *inputData, size_t inputSize)
{
    if (!hasOpenEntry())
        throw std::runtime_error("ZipCreator has no open archive entry");

    size_t bytesWritten = 0u;

    switch (d->entryInfo.type)
    {
        case EntryInfo::ZIP:
            bytesWritten = d->writeToCurrentZIPEntry(inputData, inputSize);
            break;

        case EntryInfo::LZ4:
            while (bytesWritten < inputSize)
            {
                size_t bytesLeft = inputSize - bytesWritten;
                size_t chunkBytes = std::min(bytesLeft, d->lz4Ctx.buffer.size());

                assert(inputData + bytesWritten + chunkBytes <= inputData + inputSize);
                assert(chunkBytes <= LZ4F_compressBound(d->lz4Ctx.ChunkSize, &d->lz4Ctx.lz4Prefs));

                // compress the chunk into the LZ4Context buffer
                size_t compressedSize = LZ4F_compressUpdate(
                    d->lz4Ctx.ctx,
                    d->lz4Ctx.buffer.data(), d->lz4Ctx.buffer.size(),
                    inputData + bytesWritten, chunkBytes,
                    nullptr);

                if (LZ4F_isError(compressedSize))
                    throw std::runtime_error("LZ4F_compressUpdate: " + std::to_string(compressedSize));

                // flush the LZ4 buffer contents to the ZIP
                d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), compressedSize);

                bytesWritten += chunkBytes;
                d->entryInfo.bytesWritten += chunkBytes;
                d->entryInfo.lz4CompressedBytesWritten += compressedSize;
            }
            break;
    };

    return bytesWritten;
}

void ZipCreator::closeCurrentEntry()
{
    if (!hasOpenEntry())
        throw std::runtime_error("ZipCreator has no open archive entry");

    if (d->entryInfo.type == EntryInfo::LZ4)
    {
        // flush whatever remains within internal buffers
        size_t const compressedSize = LZ4F_compressEnd(
            d->lz4Ctx.ctx,
            d->lz4Ctx.buffer.data(), d->lz4Ctx.buffer.size(),
            nullptr);

        if (LZ4F_isError(compressedSize))
            throw std::runtime_error("LZ4F_compressEnd: " + std::to_string(compressedSize));

        // flush the LZ4 buffer contents to the ZIP
        size_t bytesWritten = d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), compressedSize);

        d->entryInfo.bytesWritten += bytesWritten;
        d->entryInfo.lz4CompressedBytesWritten += compressedSize;
    }

    if (auto err = mz_zip_writer_entry_close(d->mz_zipWriter))
        throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));

    d->entryInfo.isOpen = false;
}

// ZipReadHandle

size_t ZipReadHandle::read(u8 *dest, size_t maxSize)
{
    return m_zipReader->readCurrentEntry(dest, maxSize);
}

void ZipReadHandle::seek(size_t pos)
{
    std::string currentName = m_zipReader->currentEntryName();
    m_zipReader->closeCurrentEntry();
    m_zipReader->openEntry(currentName);

    std::vector<u8> buffer(Megabytes(1));

    while (pos > 0)
    {
        pos -= read(buffer.data(), std::min(buffer.size(), pos));
    }
}

// ZipReader

ZipReader::ZipReader()
    : m_readHandle(this)
{
    mz_zip_reader_create(&m_reader);
    mz_stream_os_create(&m_osStream);
}

ZipReader::~ZipReader()
{
    mz_stream_os_delete(&m_osStream);
    mz_zip_reader_delete(&m_reader);
}

void ZipReader::openArchive(const std::string &archiveName)
{
    if (auto err = mz_stream_os_open(m_osStream, archiveName.c_str(), MZ_OPEN_MODE_READ))
        throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

    if (auto err = mz_zip_reader_open(m_reader, m_osStream))
        throw std::runtime_error("mz_zip_reader_open: " + std::to_string(err));

    if (auto err = mz_zip_reader_goto_first_entry(m_reader))
    {
        if (err != MZ_END_OF_LIST)
            throw std::runtime_error("mz_zip_reader_goto_first_entry: " + std::to_string(err));
    }

    m_entryListCache = {};
    s32 err = MZ_OK;

    do
    {
        mz_zip_file *entryInfo = nullptr;
        if ((err = mz_zip_reader_entry_get_info(m_reader, &entryInfo)))
            throw std::runtime_error("mz_zip_reader_entry_get_info: " + std::to_string(err));

        m_entryListCache.push_back(entryInfo->filename);
    } while ((err = mz_zip_reader_goto_next_entry(m_reader)) == MZ_OK);

    if (err != MZ_END_OF_LIST)
        throw std::runtime_error("mz_zip_reader_goto_next_entry: " + std::to_string(err));
}

void ZipReader::closeArchive()
{
    if (auto err = mz_zip_reader_close(m_reader))
        throw std::runtime_error("mz_zip_reader_close: " + std::to_string(err));

    if (auto err = mz_stream_os_close(m_osStream))
        throw std::runtime_error("mz_stream_os_close: " + std::to_string(err));

    m_entryListCache = {};
}

std::vector<std::string> ZipReader::entryList()
{
    return m_entryListCache;
}

ZipReadHandle *ZipReader::openEntry(const std::string &name)
{
    if (auto err = mz_zip_reader_locate_entry(m_reader, name.c_str(), false))
        throw std::runtime_error("mz_zip_reader_locate_entry: " + std::to_string(err));

    if (auto err = mz_zip_reader_entry_open(m_reader))
        throw std::runtime_error("mz_zip_reader_entry_open: " + std::to_string(err));

    m_currentEntryName = name;

    return &m_readHandle;
}

ZipReadHandle *ZipReader::currentEntry()
{
    return &m_readHandle;
}

void ZipReader::closeCurrentEntry()
{
    if (auto err = mz_zip_reader_entry_close(m_reader))
        throw std::runtime_error("mz_zip_reader_entry_close: " + std::to_string(err));
}

size_t ZipReader::readCurrentEntry(u8 *dest, size_t maxSize)
{
    s32 res = mz_zip_reader_entry_read(m_reader, dest, maxSize);

    if (res < 0)
        throw std::runtime_error("mz_zip_reader_entry_read: " + std::to_string(res));

    return static_cast<size_t>(res);
}

std::string ZipReader::currentEntryName() const
{
    return m_currentEntryName;
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
