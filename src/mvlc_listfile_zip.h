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

class ZipEntryWriteHandle;

class MESYTEC_MVLC_EXPORT ZipCreator
{
    public:
        struct EntryInfo
        {
            enum Type { ZIP, LZ4 };
            Type type = ZIP;
            std::string name;
            bool isOpen = false;
            size_t bytesWritten = 0u;
            size_t lz4CompressedBytesWritten = 0u;
        };

        ZipCreator();
        ~ZipCreator();

        void createArchive(const std::string &zipFilename);
        void closeArchive();
        bool isOpen() const;

        ZipEntryWriteHandle *createZIPEntry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle *createZIPEntry(const std::string &entryName)
        { return createZIPEntry(entryName, 1); }

        ZipEntryWriteHandle *createLZ4Entry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle *createLZ4Entry(const std::string &entryName)
        { return createLZ4Entry(entryName, 0); };

        bool hasOpenEntry() const;
        const EntryInfo &entryInfo() const;

        size_t writeToCurrentEntry(const u8 *data, size_t size);

        void closeCurrentEntry();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class MESYTEC_MVLC_EXPORT ZipEntryWriteHandle: public WriteHandle
{
    public:
        ~ZipEntryWriteHandle() override;
        size_t write(const u8 *data, size_t size) override;

    private:
        friend class ZipCreator;
        explicit ZipEntryWriteHandle(ZipCreator *creator);
        ZipCreator *m_zipCreator = nullptr;
};

class ZipReader;

class MESYTEC_MVLC_EXPORT ZipReadHandle: public ReadHandle
{
    public:
        ZipReadHandle(ZipReader *reader)
            : m_zipReader(reader)
        { }

        size_t read(u8 *dest, size_t maxSize) override;
        void seek(size_t pos) override;

    private:
        ZipReader *m_zipReader = nullptr;
};

class MESYTEC_MVLC_EXPORT ZipReader
{
    public:
        ZipReader();
        ~ZipReader();

        void openArchive(const std::string &archiveName);
        void closeArchive();
        std::vector<std::string> entryList();

        ZipReadHandle *openEntry(const std::string &name);
        ZipReadHandle *currentEntry();
        void closeCurrentEntry();
        size_t readCurrentEntry(u8 *dest, size_t maxSize);
        std::string currentEntryName() const;

    private:
        void *m_reader = nullptr;
        void *m_osStream = nullptr;
        std::vector<std::string> m_entryListCache;
        ZipReadHandle m_readHandle;
        std::string m_currentEntryName;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
