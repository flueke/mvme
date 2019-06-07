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
        ListFile(const QString &fileName);

        /* For ZIP file input it is assumed that the file has been opened before
         * being passed to our constructor. */
        ListFile(QuaZipFile *inFile);
        ~ListFile();

        bool open();
        QJsonObject getDAQConfig();
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

class LIBMVME_EXPORT ListFileReader: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void replayStopped();
        void replayPaused();

    public:
        using LoggerFun = std::function<void (const QString &)>;

        ListFileReader(DAQStats &stats, QObject *parent = 0);
        ~ListFileReader();
        void setListFile(ListFile *listFile);
        ListFile *getListFile() const { return m_listFile; }

        bool isRunning() const { return m_state != DAQState::Idle; }
        DAQState getState() const { return m_state; }

        void setEventsToRead(u32 eventsToRead);

        void setLogger(LoggerFun logger) { m_logger = logger; }

        ThreadSafeDataBufferQueue *m_freeBuffers = nullptr;
        ThreadSafeDataBufferQueue *m_fullBuffers = nullptr;

    public slots:
        void start();
        void stop();
        void pause();
        void resume();

    private:
        void mainLoop();
        void setState(DAQState state);
        void logMessage(const QString &str);

        DAQStats &m_stats;

        DAQState m_state;
        std::atomic<DAQState> m_desiredState;

        ListFile *m_listFile = 0;

        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;

        u32 m_eventsToRead = 0;
        bool m_logBuffers = false;
        LoggerFun m_logger;
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

struct LIBMVME_EXPORT OpenListfileResult
{
    std::unique_ptr<ListFile> listfile;
    QByteArray messages;                    // messages.log if found
    QByteArray analysisBlob;                // analysis config contents
    QString analysisFilename;               // analysis filename inside the archive

    OpenListfileResult() = default;

    OpenListfileResult(OpenListfileResult &&) = default;
    OpenListfileResult &operator=(OpenListfileResult &&) = default;

    OpenListfileResult(const OpenListfileResult &) = delete;
    OpenListfileResult &operator=(const OpenListfileResult &) = delete;
};

OpenListfileResult LIBMVME_EXPORT open_listfile(const QString &filename);

std::unique_ptr<VMEConfig> LIBMVME_EXPORT read_config_from_listfile(ListFile *listfile);


#endif /* __MVME_LISTFILE_UTILS_H__ */
