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

class ZipEntryWriteHandle3;

class MESYTEC_MVLC_EXPORT ZipCreator3
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

        ZipCreator3();
        ~ZipCreator3();

        void createArchive(const std::string &zipFilename);
        void closeArchive();
        bool isOpen() const;

        ZipEntryWriteHandle3 *createZIPEntry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle3 *createZIPEntry(const std::string &entryName)
        { return createZIPEntry(entryName, 1); }

        ZipEntryWriteHandle3 *createLZ4Entry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle3 *createLZ4Entry(const std::string &entryName)
        { return createLZ4Entry(entryName, 0); };

        bool hasOpenEntry() const;
        const EntryInfo &entryInfo() const;

        size_t writeToCurrentEntry(const u8 *data, size_t size);

        void closeCurrentEntry();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class MESYTEC_MVLC_EXPORT ZipEntryWriteHandle3: public WriteHandle
{
    public:
        ~ZipEntryWriteHandle3() override;
        size_t write(const u8 *data, size_t size) override;

    private:
        friend class ZipCreator3;
        explicit ZipEntryWriteHandle3(ZipCreator3 *creator);
        ZipCreator3 *m_zipCreator = nullptr;
};


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

#if 0
typedef struct {
    int error;
    unsigned long long size_in;
    unsigned long long size_out;
} compressResult_t;

static compressResult_t
compress_file_internal(FILE* f_in, FILE* f_out,
                       LZ4F_compressionContext_t ctx,
                       void* inBuff,  size_t inChunkSize,
                       void* outBuff, size_t outCapacity)
{
    compressResult_t result = { 1, 0, 0 };  /* result for an error */
    unsigned long long count_in = 0, count_out;

    assert(f_in != NULL); assert(f_out != NULL);
    assert(ctx != NULL);
    assert(outCapacity >= LZ4F_HEADER_SIZE_MAX);
    assert(outCapacity >= LZ4F_compressBound(inChunkSize, &kPrefs));

    /* write frame header */
    {   size_t const headerSize = LZ4F_compressBegin(ctx, outBuff, outCapacity, &kPrefs);
        if (LZ4F_isError(headerSize)) {
            printf("Failed to start compression: error %u \n", (unsigned)headerSize);
            return result;
        }
        count_out = headerSize;
        printf("Buffer size is %u bytes, header size %u bytes \n",
                (unsigned)outCapacity, (unsigned)headerSize);
        safe_fwrite(outBuff, 1, headerSize, f_out);
    }

    /* stream file */
    for (;;) {
        size_t const readSize = fread(inBuff, 1, IN_CHUNK_SIZE, f_in);
        if (readSize == 0) break; /* nothing left to read from input file */
        count_in += readSize;

        size_t const compressedSize = LZ4F_compressUpdate(ctx,
                                                outBuff, outCapacity,
                                                inBuff, readSize,
                                                NULL);
        if (LZ4F_isError(compressedSize)) {
            printf("Compression failed: error %u \n", (unsigned)compressedSize);
            return result;
        }

        printf("Writing %u bytes\n", (unsigned)compressedSize);
        safe_fwrite(outBuff, 1, compressedSize, f_out);
        count_out += compressedSize;
    }

    /* flush whatever remains within internal buffers */
    {   size_t const compressedSize = LZ4F_compressEnd(ctx,
                                                outBuff, outCapacity,
                                                NULL);
        if (LZ4F_isError(compressedSize)) {
            printf("Failed to end compression: error %u \n", (unsigned)compressedSize);
            return result;
        }

        printf("Writing %u bytes \n", (unsigned)compressedSize);
        safe_fwrite(outBuff, 1, compressedSize, f_out);
        count_out += compressedSize;
    }

    result.size_in = count_in;
    result.size_out = count_out;
    result.error = 0;
    return result;
}

static compressResult_t
compress_file(FILE* f_in, FILE* f_out)
{
    assert(f_in != NULL);
    assert(f_out != NULL);

    /* ressource allocation */
    LZ4F_compressionContext_t ctx;
    size_t const ctxCreation = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);
    void* const src = malloc(IN_CHUNK_SIZE);
    size_t const outbufCapacity = LZ4F_compressBound(IN_CHUNK_SIZE, &kPrefs);   /* large enough for any input <= IN_CHUNK_SIZE */
    void* const outbuff = malloc(outbufCapacity);

    compressResult_t result = { 1, 0, 0 };  /* == error (default) */
    if (!LZ4F_isError(ctxCreation) && src && outbuff) {
        result = compress_file_internal(f_in, f_out,
                                        ctx,
                                        src, IN_CHUNK_SIZE,
                                        outbuff, outbufCapacity);
    } else {
        printf("error : ressource allocation failed \n");
    }

    LZ4F_freeCompressionContext(ctx);   /* supports free on NULL */
    free(src);
    free(outbuff);
    return result;
}
#endif

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
