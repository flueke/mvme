#ifndef __MVME_LISTFILE_UTILS_H__
#define __MVME_LISTFILE_UTILS_H__

#include <QDebug>
#include <QFile>
#include <QJsonObject>
#include <QTextStream>

#include "globals.h"
#include "data_buffer_queue.h"
#include "util.h"
#include "mvme_listfile.h"
#include "libmvme_export.h"

void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer,
                      const ListfileConstants &lfc, bool dumpData=false);

class VMEConfig;
class QuaZipFile;

class LIBMVME_EXPORT ListFile
{
    public:
        // It is assumed that the input device is opened for reading when
        // passed to this constructor. The ListFile does not take ownership of
        // the input device.
        ListFile(QIODevice *input = nullptr);

        bool open();
        QJsonObject getVMEConfigJSON();
        bool seekToFirstSection();
        bool readNextSection(DataBuffer *buffer);
        s32 readSectionsIntoBuffer(DataBuffer *buffer);
        const QIODevice *getInputDevice() const { return m_input; }
        qint64 size() const;
        QString getFileName() const;
        QString getFullName() const; // filename or zipname:/filename
        u32 getFileVersion() const { return m_fileVersion; }

        // Will be empty vector for version 0 or contain "MVME<version>" with
        // version being an u32.
        QVector<u8> getPreambleBuffer() const { return m_preambleBuffer; }

    private:
        bool seek(qint64 pos);

        QIODevice *m_input = nullptr;
        QJsonObject m_configJson;
        u32 m_fileVersion = 0;
        u32 m_sectionHeaderBuffer = 0;
        QVector<u8> m_preambleBuffer;
};


class LIBMVME_EXPORT ListFileWriter: public QObject
{
    Q_OBJECT
    public:
        explicit ListFileWriter(QObject *parent = 0);
        ListFileWriter(QIODevice *outputDevice, QObject *parent = 0);

        void setOutputDevice(QIODevice *device);
        QIODevice *outputDevice() const { return m_out; }
        u64 bytesWritten() const { return m_bytesWritten; }

        bool writePreamble();
        bool writeConfig(const VMEConfig *vmeConfig);
        bool writeConfig(QByteArray contents);
        bool writeBuffer(const char *buffer, size_t size);
        bool writeBuffer(const DataBuffer &buffer);
        bool writeEndSection();
        bool writeTimetickSection();
        bool writePauseSection(ListfileSections::PauseAction pauseAction);

    private:
        bool writeStringSection(ListfileSections::SectionType sectionType,
                                const QByteArray &contents);

        QIODevice *m_out = nullptr;
        u64 m_bytesWritten = 0;
};

std::pair<std::unique_ptr<VMEConfig>, std::error_code> LIBMVME_EXPORT
    read_config_from_listfile(ListFile *listfile);


#endif /* __MVME_LISTFILE_UTILS_H__ */
