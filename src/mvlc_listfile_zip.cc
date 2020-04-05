#include "mvlc_listfile_zip.h"

#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
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

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
