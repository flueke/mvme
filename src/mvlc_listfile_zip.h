#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__

#include "mvlc_listfile.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

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

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
