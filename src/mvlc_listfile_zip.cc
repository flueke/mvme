#include "mvlc_listfile_zip.h"

#include <cassert>

#include <mz.h>
#include <mz_compat.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <sys/stat.h>
#include <time.h>

#include "util/storage_sizes.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

#if 0
ListfileZIPHandle::ListfileZIPHandle()
{
    mz_stream_os_create(&m_stream);
    mz_zip_reader_create(&m_reader);
    mz_zip_writer_create(&m_writer);

    mz_zip_writer_set_compress_method(m_writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(m_writer, 1);
    mz_zip_writer_set_follow_links(m_writer, true);

}


ListfileZIPHandle::~ListfileZIPHandle()
{
    try
    {
        close();
    }
    catch (...)
    {}

    mz_zip_writer_delete(&m_writer);
    mz_zip_reader_delete(&m_reader);
    mz_stream_os_delete(&m_stream);

}

std::ios_base::openmode ListfileZIPHandle::openMode() const
{
    return m_mode;
}

bool ListfileZIPHandle::isOpen() const
{
    return mz_stream_os_is_open(m_stream) == MZ_OK;
}

bool ListfileZIPHandle::atEnd() const
{
    return m_endOfStream;
}

size_t ListfileZIPHandle::write(const u8 *data, size_t size)
{
    s32 bytesWritten = mz_zip_writer_entry_write(m_writer, data, size);

    if (bytesWritten < 0)
        throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

    return static_cast<size_t>(bytesWritten);
}

size_t ListfileZIPHandle::read(u8 *data, size_t maxSize)
{
    s32 bytesRead = mz_zip_reader_entry_read(m_reader, data, maxSize);

    if (bytesRead < 0)
        throw std::runtime_error("mz_zip_reader_entry_read: " + std::to_string(bytesRead));

    if (bytesRead == 0)
        m_endOfStream = true;

    return static_cast<size_t>(bytesRead);
}


void ListfileZIPHandle::seek(size_t pos)
{
    close();
    open(m_archiveName, m_entryName, m_mode);

    std::vector<u8> buffer(Megabytes(1));

    while (pos > 0)
    {
        pos -= read(buffer.data(), std::min(buffer.size(), pos));
    }
}

void ListfileZIPHandle::close()
{
    if (!isOpen())
        throw std::runtime_error("listfile not open");

    if (m_mode & std::ios_base::in)
        mz_zip_reader_close(m_reader);

    if (m_mode & std::ios_base::out)
        mz_zip_writer_close(m_writer);

    mz_stream_os_close(m_stream);
}

void ListfileZIPHandle::open(
    const std::string &archiveName,
    const std::string &entryName,
    std::ios_base::openmode mode)
{
    if (isOpen())
        throw std::runtime_error("zip is open");

    m_archiveName = archiveName;
    m_entryName = entryName;
    m_mode = mode;

    if (m_mode & std::ios_base::in)
    {
        if (auto err = mz_stream_os_open(m_stream, m_archiveName.c_str(), MZ_OPEN_MODE_READ))
            throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

        if (auto err = mz_zip_reader_open(m_reader, m_stream))
            throw std::runtime_error("mz_zip_reader_open: " + std::to_string(err));

        if (auto err = mz_zip_reader_locate_entry(m_reader, m_entryName.c_str(), false))
            throw std::runtime_error("mz_zip_reader_locate_entry: " + std::to_string(err));

        if (auto err = mz_zip_reader_entry_open(m_reader))
            throw std::runtime_error("mz_zip_reader_entry_open: " + std::to_string(err));
    }
    else if (m_mode & std::ios_base::out)
    {
        s32 mzMode = MZ_OPEN_MODE_WRITE;

        if (m_mode & std::ios_base::trunc)
            mzMode |= MZ_OPEN_MODE_CREATE;
        else
            mzMode |= MZ_OPEN_MODE_APPEND;

        if (auto err = mz_stream_os_open(m_stream, m_archiveName.c_str(), mzMode))
            throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

        if (auto err = mz_zip_writer_open(m_writer, m_stream))
            throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));

        mz_zip_file file_info = {};

        file_info.filename = m_entryName.c_str();
        file_info.modified_date = time(nullptr);
        file_info.version_madeby = MZ_VERSION_MADEBY;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_FORCE;
        file_info.external_fa = (S_IFREG) | (0644u << 16);

        if (auto err = mz_zip_writer_entry_open(m_writer, &file_info))
            throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));
    }
}

size_t ZipWriteHandle::write(const u8 *data, size_t size)
{
    return m_zipCreator->writeCurrentEntry(data, size);
}

ZipCreator::ZipCreator()
    : m_entryHandle(this)
{
}

ZipCreator::~ZipCreator()
{
    try
    {
        closeCurrentEntry();

        mz_zip_writer_close(m_writer);
        mz_stream_close(m_bufStream);

        mz_zip_writer_delete(&m_writer);
        mz_stream_delete(&m_bufStream);
        mz_stream_delete(&m_osStream);
    }
    catch (...)
    {}
}

void ZipCreator::createArchive(const std::string &archiveName)
{
    if (m_osStream || m_writer)
        throw std::runtime_error("createArchive: archive is open");

    mz_stream_os_create(&m_osStream);
    mz_stream_buffered_create(&m_bufStream);

    mz_stream_set_base(m_bufStream, m_osStream);

    mz_zip_writer_create(&m_writer);

    mz_zip_writer_set_compress_method(m_writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(m_writer, 1);
    mz_zip_writer_set_follow_links(m_writer, true);

    s32 mzMode = MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE;

    if (auto err = mz_stream_open(m_bufStream, archiveName.c_str(), mzMode))
        throw std::runtime_error("mz_stream_open: " + std::to_string(err));

    //if (auto err = mz_stream_os_open(m_osStream, archiveName.c_str(), mzMode))
    //    throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

    //if (auto err = mz_stream_buffered_open(m_bufStream, nullptr, mzMode))
    //    throw std::runtime_error("mz_stream_buffered_open: " + std::to_string(err));

    if (auto err = mz_zip_writer_open(m_writer, m_bufStream))
        throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));
}

ZipWriteHandle *ZipCreator::createEntry(const std::string &entryName)
{
    mz_zip_file file_info = {};
    file_info.filename = entryName.c_str();
    file_info.modified_date = time(nullptr);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    file_info.zip64 = MZ_ZIP64_FORCE;
    file_info.external_fa = (S_IFREG) | (0644u << 16);

    if (auto err = mz_zip_writer_entry_open(m_writer, &file_info))
        throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

    return &m_entryHandle;
}

void ZipCreator::closeCurrentEntry()
{
    if (auto err = mz_zip_writer_entry_close(m_writer))
        throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));
}

size_t ZipCreator::writeCurrentEntry(const u8 *data, size_t size)
{
    s32 bytesWritten = mz_zip_writer_entry_write(m_writer, data, size);

    if (bytesWritten < 0)
        throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

    if (static_cast<size_t>(bytesWritten) != size)
        throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

    return static_cast<size_t>(bytesWritten);
}
#endif

size_t ZipWriteHandle2::write(const u8 *data, size_t size)
{
    return m_zipCreator->writeCurrentEntry(data, size);
}

ZipCreator2::ZipCreator2()
    : m_entryHandle(this)
{
}

ZipCreator2::~ZipCreator2()
{
    try
    {
        closeCurrentEntry();

        zipClose(m_zipFile, "");
    }
    catch (...)
    {}
}

void ZipCreator2::createArchive(const std::string &archiveName)
{
    if (m_zipFile)
        throw std::runtime_error("createArchive: archive is open");

    m_zipFile = zipOpen64(archiveName.c_str(), APPEND_STATUS_CREATE);

    //mz_zip_writer_set_compress_method(m_writer, MZ_COMPRESS_METHOD_DEFLATE);
    //mz_zip_writer_set_compress_level(m_writer, 1);
    //mz_zip_writer_set_follow_links(m_writer, true);
}

ZipWriteHandle2 *ZipCreator2::createEntry(const std::string &entryName)
{
    zip_fileinfo info = {};

    time_t now = time(nullptr);
#ifdef __WIN32
    localtime_s(&info.tmz_date, &now);
#else
    localtime_r(&now, &info.tmz_date);
#endif
    info.external_fa = (S_IFREG) | (0644u << 16);

    int res = zipOpenNewFileInZip_64(
        m_zipFile,
        entryName.c_str(),
        &info,
        nullptr, 0,
        nullptr, 0,
        "",
        MZ_COMPRESS_METHOD_DEFLATE,
        1,
        true);

    if (res)
        throw std::runtime_error("zipOpenNewFileInZip_64: " + std::to_string(res));

    return &m_entryHandle;
}

void ZipCreator2::closeCurrentEntry()
{
    if (auto err = zipCloseFileInZip64(m_zipFile))
        throw std::runtime_error("zipCloseFileInZip64: " + std::to_string(err));
}

size_t ZipCreator2::writeCurrentEntry(const u8 *data, size_t size)
{
    if (auto err = zipWriteInFileInZip(m_zipFile, data, size))
        throw std::runtime_error("zipWriteInFileInZip: " + std::to_string(err));

    return size;
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
