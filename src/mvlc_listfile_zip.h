#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__

#include "mvlc_listfile.h"

#include "mesytec-mvlc_export.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

#if 0
class ListfileZIPHandle: public ListfileHandle
{
    public:
        ListfileZIPHandle();
        ~ListfileZIPHandle() override;

        std::ios_base::openmode openMode() const override;

        bool isOpen() const override;
        bool atEnd() const override;

        size_t write(const u8 *data, size_t size) override;
        size_t read(u8 *data, size_t maxSize) override;

        void seek(size_t pos) override;
        void close() override;

        void open(
            const std::string &archiveName, const std::string &entryName,
            std::ios_base::openmode mode);

    private:
        std::ios_base::openmode m_mode;
        void *m_reader = nullptr;
        void *m_writer = nullptr;
        void *m_stream = nullptr;
        std::string m_archiveName;
        std::string m_entryName;
        bool m_endOfStream = false;
};

class ZipCreator;

class ZipWriteHandle: public WriteHandle
{
    public:
        ZipWriteHandle(ZipCreator *creator)
            : m_zipCreator(creator)
        { }

        size_t write(const u8 *data, size_t size);

    private:
        ZipCreator *m_zipCreator = nullptr;
};

class ZipCreator
{
    public:
        ZipCreator();
        ~ZipCreator();
        void createArchive(const std::string &zipFilename);

        ZipWriteHandle *createEntry(const std::string &entryName);
        ZipWriteHandle *currentEntry() { return &m_entryHandle; }
        void closeCurrentEntry();
        size_t writeCurrentEntry(const u8 *data, size_t size);

    private:
        void *m_osStream = nullptr;
        void *m_bufStream = nullptr;
        void *m_writer = nullptr;
        ZipWriteHandle m_entryHandle;
};
#endif

class ZipCreator2;

class MESYTEC_MVLC_EXPORT ZipWriteHandle2: public WriteHandle
{
    public:
        ZipWriteHandle2(ZipCreator2 *creator)
            : m_zipCreator(creator)
        { }

        size_t write(const u8 *data, size_t size);

    private:
        ZipCreator2 *m_zipCreator = nullptr;
};

class MESYTEC_MVLC_EXPORT ZipCreator2
{
    public:
        ZipCreator2();
        ~ZipCreator2();
        void createArchive(const std::string &zipFilename);

        ZipWriteHandle2 *createEntry(const std::string &entryName);
        ZipWriteHandle2 *currentEntry() { return &m_entryHandle; }
        void closeCurrentEntry();
        size_t writeCurrentEntry(const u8 *data, size_t size);

    private:
        void *m_zipFile = nullptr;
        ZipWriteHandle2 m_entryHandle;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
